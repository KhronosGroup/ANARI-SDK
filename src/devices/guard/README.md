# The `guard` device

`guard` is an ANARI device that runs another ANARI device, the *wrapped*
device, in a process of its own, the *host*. If the wrapped device crashes,
the host dies and the application does not. An application gets this by
loading `guard` instead of its device; nothing else in it changes.

What is guarded is a correct program against a faulty device. A program that
is valid ANARI behaves through `guard` as it does with the wrapped device
loaded in-process. `guard` is not a sandbox and gives no security isolation.

Linux only, for now.

## Building

Configure the SDK with `-DBUILD_GUARD_DEVICE=ON`. This builds

- `anari_library_guard`, so `anariLoadLibrary("guard", ...)` loads it: the
  *adapter*, the part of the device that lives in the application;
- `anariGuardHost`, the host executable, which the adapter starts.

It needs Linux and glibc 2.39 or later (for `pidfd_spawn()`), and nothing
outside the SDK. It also builds on its own against an installed SDK:

```
cmake -S src/devices/guard -B build -DCMAKE_PREFIX_PATH=<SDK install>
```

## Using it

```
ANARI_LIBRARY=guard ANARI_GUARD_WRAPPED_DEVICE=helide anariRenderTests
```

The wrapped device is named by the device subtype given to `anariNewDevice`,
as `[subtype@]library`:

| Subtype | Wrapped device |
| --- | --- |
| `helide` | the `default` device of library `helide` |
| `fast@mylib` | device subtype `fast` of library `mylib` |
| `default` | what `ANARI_GUARD_WRAPPED_DEVICE` names, in the same form |

`anariNewDevice` starts one host for each device, waits for the wrapped
device to exist, and returns `NULL` if the host or the wrapped device cannot
be brought up. Releasing the device ends its host. An application that goes
away without releasing the device takes the host with it.

The adapter looks for the host executable in `ANARI_GUARD_HOST` if that is
set, then beside the adapter's own library, then in `../bin/` from there.

## When the wrapped device dies

*Device death* is the host process ending for any reason other than the
application releasing the device. A host that hangs is not detected.

- The application is told once, with a status message of severity
  `ANARI_SEVERITY_FATAL_ERROR` that says how the host ended. It is delivered
  on the thread that makes the first ANARI call on the dead device, after the
  wrapped device's own last messages. An application that makes no further
  call is not told.
- Every handle stays valid. Every later call does nothing and returns at
  once: `anariFrameReady` returns 1, `anariMapFrame` returns `NULL`, and
  properties are not found, except introspection answers the adapter already
  holds.
- A thread blocked in a call when the host dies returns. A frame that was
  rendering gets its completion callback once.
- Memory the application has mapped (arrays, frame channels) stays mapped and
  readable.
- A dead device never comes back. Release it and make another.

Because the death message arrives under one of the application's own calls,
its status callback may exit the process or throw from it, as it may
in-process. Two things to know:

- libanari as built by default (`ANARI_FRONTEND_CATCH_EXCEPTIONS`) terminates
  on anything a device call throws, for any device. An exception from the
  callback reaches the application only with that option off.
- In an application with several threads, exiting from the callback races
  with the other threads, as it does anywhere.

## Differences from the wrapped device in-process

- **Status messages of the wrapped device arrive on a thread of the adapter**,
  not on the thread that made the call that caused them, and some time after
  it. A status callback that exits or throws on them is not safe. The same
  holds for frame-completion callbacks, which may make any ANARI call.
- **What cannot cross to another process is refused.** Parameters that are
  raw pointers or native handles, objects of another device, and extension
  functions (`anariGetProcAddress`) are rejected with a status error. The
  extensions that depend on them (`ANARI_KHR_DATA_PARALLEL_MPI`,
  `ANARI_NV_ARRAY_CUDA`, `ANARI_NV_FRAME_BUFFERS_CUDA`) are left out of the
  extension lists `guard` returns.
- **Elements of an object array that are not handles of this device** go to
  the wrapped device as `NULL`, with one error status message. Where the
  wrapped device has `ANARI_KHR_ARRAY1D_REGION`, a program may leave invalid
  handles outside the array's `region`; through `guard` that program gets
  this message, which it would not in-process.
- **A handle released by the application and left in an object array** it
  then maps and unmaps keeps naming its object, as in-process. A released
  handle written anywhere new is misuse.
- **Unloading the `guard` library from inside the callback that released the
  device** is not supported.

## Threads

`guard` has `ANARI_KHR_DEVICE_SYNCHRONIZATION` when the wrapped device has
it, and then promises what that extension says: threads may call at the same
time on different objects. Calls that share an object, calls on the device
itself and `anariRenderFrame` stay the application's to synchronise.

- A thread waiting for an answer from the host holds up every other thread's
  calls until it returns. A thread blocked in `anariFrameReady(ANARI_WAIT)`
  does not: that wait is answered by the adapter.
- At device death, exactly one of several calling threads delivers the death
  message; the rest return at once.
- The death message can run at the same time as a frame-completion callback.

## Cost

Calls that return nothing are sent without waiting for the host; a call that
returns a value is a round trip to the host. Array data of 1 MB or more
crosses in memory shared with the host: a managed array or a mapped parameter
array is written straight into it, a shared array is copied once. A smaller
array crosses in its message the first time and is given shared memory when
it is written again. The wrapped device never copies.

The cost that grows is frame readback. `anariMapFrame` is a round trip in
which the host copies the channel once, so each frame costs more with every
byte the application maps: on the machine the device was developed on, some
30 to 70 microseconds for each megabyte, and under half a millisecond a frame
in all for a 1920×1080 `UFIXED8_RGBA_SRGB` frame. `guardBench`, built with
the device and never run by `ctest`, measures all of this against the same
device in-process and writes a report:

```
guardBench out=overhead.md [devices=helide,sink]
```

## Diagnostics

None of these is a promise; they exist for measuring.

| Device property (`ANARI_UINT64`) | Meaning |
| --- | --- |
| `guard.bytesSent` | bytes the adapter has sent to the host |
| `guard.bytesReceived` | bytes it has received |
| `guard.flushes` | writes to the call channel |
| `guard.regionsCreated` | blocks of shared memory made |

| Environment variable | Meaning |
| --- | --- |
| `ANARI_GUARD_INLINE_THRESHOLD` | bytes from which an array crosses in shared memory from the start (default 1048576) |
| `ANARI_GUARD_PROBE_PERIOD_US` | how often, in microseconds, the host asks the wrapped device whether an outstanding render is over (default 1000) |

## Tests

The tests are named `guard_*`, are built with `BUILD_TESTING`, and need
helide (`BUILD_HELIDE_DEVICE`). They run the device from the build tree.

```
ctest -R '^guard_'
ctest -L death        # device death, at each point the host can die
ctest -L acceptance   # anariRenderTests through guard, against in-process
```

- `guard_acceptance_helide` runs the SDK's unmodified `anariRenderTests` on
  helide in-process and through `guard`, and requires the same images.
- The death tests use `guardTestHost`, a host that ends itself at a call named
  by `ANARI_GUARD_HOST_FAULT`, and a *recording device*
  (`anari_library_guard_recorder`) that renders nothing and writes down every
  call it is given. Neither is installed.
- `-DGUARD_SANITIZE=ON` builds the device with AddressSanitizer and
  UndefinedBehaviorSanitizer, `-DGUARD_SANITIZE_THREAD=ON` with
  ThreadSanitizer (suppressions in `tests/tsan.supp`). The tests that load
  helide into the test program do not pass under ThreadSanitizer.

## How it works

The adapter and the host are joined by two UNIX stream sockets: a *call
channel* for calls and their answers, and an *event channel* for what the
host sends unasked (status messages, frame completions). Object handles are
integers the adapter chooses, so creating an object needs no answer. Bulk
data crosses in sealed `memfd` regions that both processes map. The adapter
learns of device death from a pidfd on the host, or from either channel
failing.

In the host, one thread reads the call channel and one worker makes every
call into the wrapped device, in order. While a render is outstanding the
worker asks the wrapped device whether it is over, when the wrapped device
calls back and otherwise once every probe period.

All Linux-specific code is in `common/platform/linux/`, behind
`common/platform/Platform.h`. Besides sockets, `memfd` and pidfds, it reads
`/proc/<pid>/stat` to tell a host that is ending from one that cannot be
reached.
