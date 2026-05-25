# AGENTS.md

This file provides guidance to coding agents working in this repository.

## Overview

This directory implements the ANARI debug device (`anari_library_debug`) — a passthrough wrapper that intercepts every ANARI API call, performs validation, then forwards to a wrapped inner device. It does **not** do any rendering itself.

See the root-level `AGENTS.md` for build commands, overall SDK architecture, and coding conventions.

## How the Debug Device Works

Every ANARI API call flows through three layers in order:

1. **`DebugInterface` / `DebugBasics`** — validates the call (argument checks, ref-count checks, unknown-parameter warnings). Reports errors via `DebugDevice::reportStatus()`.
2. **`DebugDevice`** — performs handle translation (debug handles ↔ wrapped handles), forwards to the inner device, and tracks per-object state via `DebugObjectBase`.
3. **`SerializerInterface` / `CodeSerializer`** — optionally records the call to disk as reproducible C++ code.

### Handle Translation

The debug device replaces every opaque `ANARIObject` handle from the wrapped device with an integer index into `DebugDevice::objects` (a `std::vector<std::unique_ptr<DebugObjectBase>>`). The index **is** the debug handle (cast via `uintptr_t`). `objectMap` maps wrapped handles back to debug handles.

- `newHandle(T)` — creates a new `DebugObjectBase` entry and returns the debug handle.
- `unwrapHandle(T)` / `wrapHandle(T)` — convert between debug and wrapped handles.
- Object arrays (`ANARIArray` of `ANARI_OBJECT` type) require special care: the `ANARIObject*` buffer must be translated element-by-element on both `newArray1D` and `mapArray`/`unmapArray`.

### Per-Object State (`DebugObject.h`)

`DebugObjectBase` is the public interface, exposed in `include/anari/ext/debug/DebugObject.h`. Concrete types:

- `GenericDebugObject` — base; tracks ref count, uncommitted-parameter count, reference graph.
- `GenericArrayDebugObject` — adds array metadata (dimensions, type, map/unmap state, translated handle buffer).
- `DebugObject<ANARI_TYPE>` — typed specializations; `DebugObject<ANARI_FRAME>` also stores the frame-completion callback.
- `SubtypedDebugObject<ANARI_TYPE>` — adds subtype string (e.g., `"perspective"` camera).
- `ObjectFactory` — virtual factory; downstream devices can subclass this to inject custom `DebugObjectBase` subclasses for richer per-object validation.

### Validation (`DebugBasics.cpp`)

`DebugBasics` uses the `DEBUG_FUNCTION_SOURCE` macro family to look up per-object info before each call, then calls `td->reportStatus()` on any violation. Common checks include: use-after-release, unknown subtype, invalid parameter type, null pointers with non-null deleter, etc.

Parameter/object extension tracking uses the generated `anari_library_debug_queries.h` (produced by `anari_generate_queries()` from `experimental_device.json`) to map parameters to extension enum values, stored in `DebugDevice::used_features[]` and printed on device destruction.

### Serialization (`CodeSerializer`)

Activated by setting device parameters `traceMode = "code"` and `traceDir = "<dir>"` (or via `ANARI_DEBUG_TRACE_MODE` / `ANARI_DEBUG_TRACE_DIR` env vars). Writes two files to `traceDir`: a `.cpp` source file and a binary `.data` file containing raw array contents. The generated code can be compiled and run against any ANARI device to replay the captured session.

### Frame Completion Callbacks

Because `frameCompletionCallbackUserData` is intercepted and replaced with `this` (the `DebugDevice` pointer), the debug device wraps the callback via `frameContinuationWrapper` so the user's callback receives the debug-side frame handle and device.

## Extending the Debug Device

Downstream devices can provide a custom `ObjectFactory` subclass to add device-specific validation. Assign it to `DebugDevice::debugObjectFactory` after construction. Override the `new_*` virtual methods to return custom `DebugObjectBase` subclasses, and override `print_summary()` to emit validation summaries on device destruction.

## Using the Debug Device

```bash
# Wrap helide with the debug device (env var approach)
ANARI_LIBRARY=debug ANARI_DEBUG_WRAPPED_LIBRARY=helide ./my_app

# Enable code-trace serialization
ANARI_DEBUG_TRACE_MODE=code ANARI_DEBUG_TRACE_DIR=/tmp/trace ./my_app
```

Or programmatically:
```cpp
auto debugDevice = anariNewDevice(lib, "debug");
anariSetParameter(debugDevice, debugDevice, "wrappedDevice", ANARI_DEVICE, &innerDevice);
anariCommitParameters(debugDevice, debugDevice);
```
