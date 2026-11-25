# Synchronization rules are timed behaviour tests, each Case run in its own process

The CTS checks images, but the synchronization rules in the spec (thread
safety and `KHR_DEVICE_SYNCHRONIZATION`, asynchronous rendering, the frame
completion callback, array mapping) say which calls must return, not what a
frame looks like. A device that breaks one of them usually hangs rather than
returns a wrong answer. So these rules are **timed behaviour tests**: the
behaviour hook the CTS already has (a Test with `.behavior(check)` records a
pass/fail verdict and a detail in its sidecar, with no ground truth), plus two
declarations on the Test:

- `.timeout(duration)`: the runner runs each Case in a child process and kills
  it if it hasn't finished within `duration`. The Case is then recorded as
  failed, with the detail "timed out after N s". The rest of the run goes on.
- `.failOnDeviceErrors()`: the Case fails if the device reports an
  `ANARI_SEVERITY_ERROR` (or `FATAL_ERROR`) status message while the check
  runs, and the detail quotes it. The checks only make calls the spec allows,
  so the device must not refuse one with an error.

The check itself is ordinary single-threaded code against the device. A rule
that can only be broken by blocking forces the device into the state where it
would block, without relying on timing: it maps a shared array the rendered
world references, which (per `anariMapArray`) holds the device off rendering
that world. The render is then in flight, and stays in flight until the check
unmaps the array. A call that waits for that render never returns, so a hang is
the only way to fail, and the timeout catches it. A call that returns is also
timed against a generous budget (1 s), to catch calls that wait for something
shorter than the render.

## How a hang is detected and reported

`anariCts run` (the parent) runs a Case of a Test with a timeout by starting
itself again with the same arguments plus `--isolated-case
<category>/<test>/<case>`. The child loads the device, runs that one Case
in-process, and writes its sidecar as any `run` does (ADR-0003). The parent
deletes a stale sidecar for the Case first and waits for the child up to the
timeout:

- the child exits cleanly after writing a sidecar: the parent counts that
  verdict;
- the child is still running at the timeout: the parent kills it and writes a
  failed sidecar, "timed out after N s: killed the isolated process (the device
  hung)". If the child wrote a sidecar before hanging (it hung while releasing
  the device), its detail is kept too;
- the child crashes, exits with an error, or leaves no sidecar: a failed
  sidecar with the exit status.

A hang is a failed verdict with a detail. The sidecar schema doesn't change
(`detail` already exists), so the text, HTML and PDF reports show it with no
change.

When no isolation is set up, the runner runs a timed Test in-process like any
behaviour test, without enforcing the timeout. That is the case in the child
(so it doesn't start a grandchild) and in the unit tests, which drive the
runner directly and rely on the ctest `TIMEOUT` instead.

## Considered Options

- **A watchdog thread in the `anariCts` process.** Run each check on a worker
  thread; if it misses the deadline, write a failed sidecar. Rejected: a hung
  device can't be recovered in-process. The stuck thread can't be cancelled,
  it holds the device's locks and worker, and releasing the device would block
  too. The process could only exit on the spot (`std::_Exit`), losing every Case
  after the hung one, render tests included, and a crash (as likely as a hang
  in a synchronization bug) would still take the whole run down.
- **A child process per timed Case (chosen).** Only the hung Case is lost; the
  device is loaded afresh for the next Case, so one bad rule doesn't corrupt the
  others; crashes are contained as well. The cost is a device load per Case
  (tens of milliseconds for helide), a small spawn-and-wait utility with a
  POSIX and a Windows implementation, and the child needs the path to its own
  executable. Only Tests that declare a timeout pay it.
- **One child process for all the timed Cases.** Cheaper, but a hang loses
  every later timed Case, and a child that is killed can't say which Case hung
  unless it writes progress somewhere. Rejected for the few, fast Cases the
  synchronization rules need.
- **ctest `TIMEOUT` around a whole `anariCts` run.** It detects a hang but
  records nothing per Case and doesn't help a user running `anariCts` against
  their device. Kept only as the backstop for the unit tests.

## Consequences

- A killed child doesn't say which call hung. The check's `description()` lists
  its steps in order, so the reader can narrow it down; recording progress for
  the parent to report is possible later if it's needed.
- A rule a device breaks by blocking for a *bounded* time (say, while a render
  actually executes) needs a render that runs long enough to measure, which is
  device-dependent. The rules below that need it are left for later.
- The rules below don't need `KHR_DEVICE_SYNCHRONIZATION`: they never make two
  calls at once. Calls that share an object (by default, the device) "may not
  overlap", so while a completion callback may run on another thread, the check
  makes no call; it waits for the callback to finish (up to 10 s, for a device
  that runs callbacks inside `anariFrameReady(ANARI_WAIT)`) and only then waits
  on the frame. Rules with calls from several threads at once must require
  `ANARI_KHR_DEVICE_SYNCHRONIZATION`.

## The rule set

Implemented (category `synchronization`):

1. `no_wait_never_blocks` — with a render held off by a mapped shared array,
   `anariRenderFrame` (the frame's first render), `anariFrameReady(ANARI_NO_WAIT)`
   and `anariGetProperty(..., ANARI_NO_WAIT)` on the frame (`duration`) and on its
   world (`bounds`) return, within the budget; after the unmap,
   `anariFrameReady(ANARI_WAIT)` returns true. Mapping a shared array returns the
   app's pointer. Spec: rendering frames ("this call may not block";
   `ANARI_NO_WAIT` returns whether the frame completed), property queries (the
   wait mask says whether to wait "or return instantly"), `anariMapArray`.
   Whether the render really was held off (the spec says the device "should
   not" render while the array is mapped) is noted in the detail, not asserted.
   The spec doesn't list which calls are legal on the *world* a render uses
   while the render is in flight; this rule reads it as allowed (a query
   doesn't change the world), and a spec decision otherwise would drop that
   query.
2. `completion_callback_reentry` — the frame's completion callback maps and
   unmaps a shared array the frame's world references (getting the app's
   pointer), queries its own frame (`anariFrameReady(ANARI_NO_WAIT)`, `duration`
   with and without waiting), and maps the frame's color channel (the frame's
   size and type). The callback runs once, and has finished when
   `anariFrameReady(ANARI_WAIT)` returns. Spec: the frame completion callback
   ("this continuation must be complete before returning from
   `anariFrameReady()` when called with `ANARI_WAIT`"; and, in the section's
   note, "ANARI API calls are legal within the continuation, except for
   calling `anariFrameReady()` with `ANARI_WAIT`"). The rule relies on that
   note, and "runs once" is read from "a callback invoked as a continuation
   after the frame completes" (one per render).
   Whether the callback sees its frame as ready, and has a duration, is noted,
   not asserted.

Candidates, from `KHR_DEVICE_SYNCHRONIZATION` and halcyon's
`tests/test_synchronization.cpp`, each to check against the spec text before it
becomes a Test. Items marked *spec gap* need a spec decision first; helide's
current behaviour, where known, is noted, and a rule helide breaks would be
added as a failing Test and reported, not weakened.

3. Different threads may use different objects of the same device at once
   (`KHR_DEVICE_SYNCHRONIZATION`): threads committing parameters on their own
   objects, mapping their own arrays and querying properties while frames
   render, with no crash, error or hang. Needs the extension.
4. Mapping an array while renders are in flight never tears a frame: an app
   thread keeps rewriting a mapped color array (all red, then all blue) while
   frames render; every frame is uniformly one color (halcyon's stress test).
   Pass/fail from the frame's pixels, not ground truth.
5. A render issued while a shared array is mapped sees what was written before
   the unmap (halcyon "frame waits for mapped array"). The spec's "should not
   execute" makes the holding-off a recommendation; the render seeing the final
   data is the stronger, checkable half.
6. A completion callback that writes an array has the next render see it
   (halcyon "callback maps array").
7. `anariRenderFrame` doesn't block when the frame's previous render is still in
   flight ("this call may not block"). helide waits for the previous render
   (`HelideDevice::renderFrame`), and if this thread holds a mapped array it
   reports an ERROR and drops the render instead, so helide would fail this.
   *Spec gap*: whether rendering a frame that is still rendering is legal at
   all.
8. `anariRenderFrame` of a frame from its own completion callback is legal (only
   `anariFrameReady(ANARI_WAIT)` is excluded). helide ignores it with a WARNING
   ("does not support rendering a frame from its own completion callback"), so
   helide would fail this.
9. A callback of frame A may use frame B (`ANARI_NO_WAIT` queries, parameters);
   waiting on B from A's callback when B is queued behind A can't finish on a
   device with one worker. helide and halcyon report an ERROR and return.
   *Spec gap*: the spec only excludes `anariFrameReady(ANARI_WAIT)` on the
   callback's own frame.
10. `ANARI_NO_WAIT` property queries don't wait for a render that is executing
    (not just queued). helide's world `bounds` query takes the world's lock,
    which a render holds while it traces, so it blocks for the rest of that
    render. The spec says the query "should" return instantly; needs a
    long-running render (see Consequences).
11. `anariGetProperty(..., ANARI_WAIT)` sees the effect of renders issued
    before it (halcyon: a frame's `duration` is that of its latest render).
    *Spec gap*: what WAIT waits for.
12. No hang when the app waits (`anariFrameReady(ANARI_WAIT)`, `anariMapFrame`,
    `anariGetProperty(ANARI_WAIT)`) from a thread holding a mapped array a
    queued render waits for. The app causes the deadlock; helide and halcyon
    report an ERROR and don't wait. *Spec gap*: implementation-defined today, so
    at most an informative Test.
13. Releasing a mapped array unmaps it; releasing a frame that was never waited
    on doesn't leak it. *Spec gap* for the first; the second is visible only
    through a device's leak warnings.
14. From halcyon's tests, not yet placed: the device declares
    `KHR_DEVICE_SYNCHRONIZATION` if it behaves so (a query, not a behaviour);
    mapping an array twice and unmapping it once; a render dropped with an
    ERROR (candidates 7 and 9) still fires its completion callback; committing
    a frame while this thread has an array mapped doesn't wait; a status
    callback reporting during a render's commit flush may query (with
    `ANARI_WAIT`), release or unmap arrays; two threads mapping and unmapping
    one array at once stay consistent. All are *spec gaps* or app errors the
    spec leaves undefined, and some are helium internals; each needs a spec
    reading before it becomes a Test.
