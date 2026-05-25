# AGENTS.md

This file provides guidance to coding agents working in this repository.

See the root `AGENTS.md` for build commands, code style, and overall SDK architecture.

## What Helium Provides

Helium is a static library (`anari::helium`) that device implementors link against to get:
- Parameter management via `anariSetParameter`/`anariUnsetParameter`
- Deferred, priority-ordered commit buffering
- Object lifetime management with dual ref-counts (public vs. internal)
- Base classes for arrays and frames
- Array change tracking via observer pattern
- Status callback infrastructure

## Class Hierarchy

```
RefCounted
  └─ BaseObject  (+ ParameterizedObject, + LockableObject)
       ├─ BaseArray  ← host Array / Array1D / Array2D / Array3D / ObjectArray
       └─ BaseFrame
BaseDevice  (+ ParameterizedObject, + LockableObject)
  └─ has-a: BaseGlobalDeviceState (subclass to add device-specific state)
```

## Key Patterns

### Dual Reference Counts

Every `RefCounted` object has two independent counters packed into one 64-bit atomic:
- **PUBLIC** (lower 32 bits): application-held references
- **INTERNAL** (upper 32 bits): device-internal references (via `IntrusivePtr`)

When PUBLIC → 0 while INTERNAL > 0, `on_NoPublicReferences()` fires — arrays must call `privatize()` here to copy application memory before the app frees it. Frames auto-discard inflight renders.

`IntrusivePtr<T>` only touches INTERNAL refs, so using it exclusively keeps the two counts cleanly separated.

### Parameter Flow (Pull Model)

Parameters are stored generically in `ParameterizedObject` and read during `commitParameters()`:

```cpp
void MyObject::commitParameters() {
  m_radius = getParam<float>("radius", 1.0f);
  m_geometry = getParamObject<BaseObject>("geometry");  // increments INTERNAL ref
}
```

`getParamObject<T>()` returns a raw pointer; store it in an `IntrusivePtr<T>` or `ChangeObserverPtr<T>` to manage lifetime.

### Deferred Commit + Finalize (Two Phases)

`anariCommitParameters()` queues the object in `BaseGlobalDeviceState::commitBuffer` — it does **not** call `commitParameters()` immediately. The device flushes the buffer at frame start and before property queries.

During flush, two virtual methods are called in order:
1. `commitParameters()` — read parameters, update internal state
2. `finalize()` — update state that depends on other already-committed objects

Objects are flushed in priority order so dependencies are committed first:
`Frame(6) > World(5) > Instance(4) > Group(3) > Surface/Volume(2) > Material(1) > others(0)`

Timestamps skip redundant work: `commitParameters()` is called only if `lastParameterChanged > lastCommitted`.

### Change Observer Pattern

Use `ChangeObserverPtr<T>` to automatically track when a referenced object changes:

```cpp
// In MyObject (a surface, sampler, etc.):
ChangeObserverPtr<BaseArray> m_texture{this};  // 'this' registers as observer

void MyObject::commitParameters() {
  m_texture = getParamObject<BaseArray>("texture");  // old/new obs managed automatically
}
```

When the observed object calls `notifyChangeObservers()`, each observer is marked updated and re-queued for finalization. This propagates array unmaps → sampler recomputation automatically.

### Extending Global Device State

Subclass `BaseGlobalDeviceState` to share device-specific context (GPU handles, allocators, etc.) with all objects without a circular include dependency:

```cpp
struct MyDeviceState : public helium::BaseGlobalDeviceState {
  // device-specific fields
  MyRenderer *renderer{nullptr};
};

// In device constructor:
m_state = std::make_unique<MyDeviceState>(this_device_ptr);

// In objects (cast from the base pointer stored on device):
auto &s = *static_cast<MyDeviceState *>(deviceState());
```

### Intercepting BaseDevice Calls

Override any `BaseDevice` method and call the base when needed:

```cpp
void MyDevice::setParameter(ANARIObject o, const char *name, ANARIDataType t, const void *v) {
  // device-specific work (e.g., select GPU context)
  helium::BaseDevice::setParameter(o, name, t, v);
}
```

**All objects passed through the API must derive from `BaseObject`, `BaseArray`, or `BaseFrame`.**

### Array Privatization

The host `Array` classes (SHARED/CAPTURED ownership) call `privatize()` when the app releases its public reference while the device still holds an internal one. Concrete device arrays that store GPU copies must override `privatize()` to handle this ownership transition.

### Status Reporting

From any object:
```cpp
reportMessage(ANARI_SEVERITY_ERROR, "message %s", detail);
```

Routes through the application-provided status callback stored in `BaseGlobalDeviceState`.
