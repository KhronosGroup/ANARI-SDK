// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Multi-rank smoke test for the ptc device. Rank r renders one sphere at a
// distinct x position with a distinct color through ptc (wrapping helide);
// rank 0 checks that every sphere shows up in the composited image. Every rank
// but the last also renders a hidden sphere straight behind the last rank's,
// so that pixel has hits from all ranks and the nearest one must win.
// Each rank's surface has id 100 + its world rank, and the visible sphere is
// primitive 0 of its geometry, the hidden one primitive 1. With --ids, every
// rank also renders a "tie" sphere, the last primitive, at the same place, so
// that pixel has hits at equal depth from all ranks.
//
// Every rank also checks that the device reports KHR_DATA_PARALLEL_MPI and
// KHR_FRAME_COMPLETION_CALLBACK, ptc's own version, and the required ANARI 1.1
// device properties. Ranks other than the compositing root
// map the frame too: the contents are undefined there, but it must not crash.
//
// Usage: ptcSmoke [--color ufixed8|ufixed8-linear|float32|float32-vec3|none]
//                 [--depth] [--ids]
//                 [--resize] [--first-color <type>] [--unset-all]
//                 [--uncommitted] [--callback] [--reenter]
//                 [--invert-check] [--fail-last-rank] [--wrong-types]
//                 [--comm <comm>] [--first-comm <comm>]
//                 [--background none|blend|opaque]
//                 [--expect-library <path>]
//                 [--wrapped-library <name>]
//
//   --color           frame color channel type (default ufixed8); 'none'
//                     leaves channel.color unset and checks that mapping
//                     color is refused; 'float32-vec3' is a type ptc cannot
//                     composite, so committing it must report an ERROR with
//                     ANARI_STATUS_INVALID_ARGUMENT, rendering must report an
//                     ERROR through the status callback on every rank (not
//                     throw), and mapping color must be refused because the
//                     render failed
//   --depth           enable channel.depth and check the composited depth;
//                     without it, check that mapping depth is refused
//   --ids             enable channel.primitiveId, channel.objectId and
//                     channel.instanceId (UINT32) and check that each pixel
//                     holds the nearest fragment's ids; without it, check
//                     that mapping them is refused. At the tie sphere, the
//                     lowest rank's ids must win. channel.normal and
//                     channel.albedo, which ptc does not composite, must
//                     always be refused.
//   --resize          render a first frame at half the size, then resize
//   --first-color     render a first frame with this color type, then change
//                     it to --color
//   --unset-all       render a first frame, then anariUnsetAllParameters and
//                     set the final parameters again
//   --uncommitted     before the final render, set a different size and
//                     color type without committing them; they must not
//                     take effect
//   --callback        set frameCompletionCallback on the frame; every render
//                     must call it once, on every rank, with the ptc device
//                     and frame handles and the app's user data, after
//                     compositing (rank 0 checks the composited color from
//                     inside the callback). Afterwards the callback is unset
//                     and one more render must not call it. Before that,
//                     only the user data is unset, and one more render must
//                     call the callback with null user data. When the render
//                     fails (--fail-last-rank, an uncompositable --color),
//                     the callback must still be called, after the ERROR
//                     was reported.
//   --reenter         with --callback: from inside the first callback,
//                     anariRenderFrame and anariCommitParameters on the
//                     frame must each be refused with an ERROR (and not call
//                     the callback again)
//   --invert-check    expect the wrong colors, so the pixel check must fail;
//                     used to test the harness's failure path
//   --fail-last-rank  the last rank commits 'float32-vec3' color (which ptc
//                     cannot composite) for the final frame, the others
//                     --color: rendering fails on that rank only, yet every
//                     rank must report an ERROR through the status callback
//                     and return (none may hang in the compositor), and the
//                     color map is refused
//   --wrong-types     first, on a separate frame, set each parameter ptc
//                     tracks with the wrong type: none may warn when set,
//                     each must warn once, by name, at the next commit, and
//                     a second commit must not warn again
//   --comm            communicator the frame composites over (default
//                     world): 'world' leaves mpiCommunicator unset (so
//                     MPI_COMM_WORLD); 'split' splits MPI_COMM_WORLD into
//                     its lower and upper half, 'interleaved' into its even
//                     and odd ranks (both need an even number of ranks), and
//                     sets mpiCommunicator. Each part renders its own image,
//                     with colors picked by world rank, and its rank 0
//                     checks it.
//   --first-comm      render a first frame over this communicator, then
//                     change it to --comm (going back to 'world' unsets
//                     mpiCommunicator)
//   --background      renderer background of each rank (default none:
//                     transparent black). 'blend' gives every rank a
//                     different semi-transparent background, 'opaque' a
//                     different opaque one; both pick it by world rank. The
//                     background corner is a miss on every rank, so ptc
//                     composites the backgrounds there in rank order (equal
//                     depth ties go to the lower rank) with premultiplied
//                     "over"; rank 0 checks it against that blend computed
//                     here in linear float, encoded like the color channel.
//                     With 'opaque' only rank 0's background shows, so it
//                     must round-trip within 1 LSB.
//   --expect-library  fail unless the loaded libanari_library_ptc is this
//                     file (guards against picking up an installed copy
//                     instead of the one under test)
//   --wrapped-library set the device parameter 'wrappedLibrary' to this and
//                     commit the device before anything else; that must
//                     load it without any warning (the tests set
//                     PTC_ANARI_LIBRARY to a library that does not exist, so
//                     the parameter has to win over it)
//
// With --resize, --first-color, --first-comm or --unset-all, a first frame is
// rendered and its color map is checked, then the frame's parameters change and
// the full checks run on the second frame. The first frame also has the
// opposite channel.depth and id channel settings, so those are enabled or
// disabled between frames too.
// Without --unset-all, parameters that go away are unset one by one.
//
// Every rank exits non-zero if any rank recorded a failure.
//
// The program is split by theme: SmokeHarness.{h,cpp} (options, layout, scene
// and frame helpers), SmokeChecks.cpp (pixel checks), SmokeCallbacks.cpp (the
// frame completion callback) and SmokeDiagnostics.cpp (extensions, properties,
// wrong-typed parameters). This file has the command line documentation and
// the main program.

// ptc
#include "SmokeHarness.h"
// std
#include <cstdio>

using namespace ptcsmoke;

int main(int argc, char **argv)
{
  MPI_Init(&argc, &argv);
  int rank = 0, size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  Options opts;
  if (!parseArgs(argc, argv, opts)) {
    MPI_Finalize();
    return 2;
  }

  int failures = 0;

  // ranks other than the last keep --color, and expect the render to fail
  // because the last one failed
  if (opts.failLastRank && rank == size - 1)
    opts.colorType = ANARI_FLOAT32_VEC3;

  auto lib = anari::loadLibrary("ptc", statusFunc);
  if (!lib) {
    printf("FAIL: rank %d could not load the ptc library\n", rank);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  auto d = anari::newDevice(lib, "default");

  if (!opts.wrappedLibrary.empty()) {
    // the wrapped library is loaded when the device is committed
    const int warningsBefore = g_warnings;
    anari::setParameter(d, d, "wrappedLibrary", opts.wrappedLibrary.c_str());
    anari::commitParameters(d, d);
    Checker c{rank};
    c.expect(g_warnings == warningsBefore,
        "loading wrapped library '" + opts.wrappedLibrary
            + "' from the device parameter reported: " + g_lastWarning);
    failures += c.failures;
  }

  if (!opts.expectLibrary.empty()) {
    const std::string loaded = loadedPtcLibrary();
    if (loaded != canonicalPath(opts.expectLibrary)) {
      printf("FAIL: rank %d loaded ptc from '%s', expected '%s'\n",
          rank,
          loaded.c_str(),
          opts.expectLibrary.c_str());
      failures++;
    }
  }

  failures += checkDeviceConformance(lib, d, rank);
  if (opts.wrongTypes)
    failures += checkWrongTypes(d, rank);

  if ((opts.comm != CommSplit::World
          || opts.firstComm.value_or(CommSplit::World) != CommSplit::World)
      && (size < 2 || size % 2)) {
    printf("FAIL: rank %d: splitting needs an even number of ranks\n", rank);
    failures++;
    opts.comm = CommSplit::World;
    opts.firstComm.reset();
  }
  Layout finalLayout = makeLayout(opts.comm);
  Layout firstLayout = makeLayout(opts.firstComm.value_or(opts.comm));

  Scene scene = buildScene(d,
      buildWorld(d, opts.twoFrames() ? firstLayout : finalLayout, opts.ids),
      opts.background);
  const FrameState finalState{IMAGE_SIZE,
      opts.colorType,
      opts.depth,
      opts.ids,
      opts.callback,
      &finalLayout};
  auto frame = anari::newObject<anari::Frame>(d);
  g_callback = {d, frame, rank, &finalLayout, &opts};
  int renders = 0;
  // the last rank's, with --fail-last-rank
  const bool rejectsColor = !compositable(opts.colorType);

  if (opts.twoFrames()) {
    const FrameState first{opts.resize ? IMAGE_SIZE / 2u : IMAGE_SIZE,
        opts.firstColorType.value_or(opts.colorType),
        !opts.depth,
        !opts.ids,
        opts.callback,
        &firstLayout};
    setAllParameters(d, frame, scene, first);
    commitFrame(d, frame);
    g_callback.rendering = &first;
    renderAndWait(d, frame);
    renders++;
    g_callback.rendering = nullptr; // `first` goes out of scope
    if (firstLayout.rank == 0)
      failures += checkFirstFrame(d, frame, first);
    if (firstLayout.worldRanks != finalLayout.worldRanks) {
      // spheres are placed and colored by rank in the frame's communicator
      anari::release(d, scene.world);
      scene.world = buildWorld(d, finalLayout, opts.ids);
      anari::setParameter(d, frame, "world", scene.world);
    }
    changeParameters(d, frame, scene, first, finalState, opts.unsetAll);
  } else {
    setAllParameters(d, frame, scene, finalState);
    commitFrame(d, frame);
  }

  // Every rank: a color type ptc cannot composite is rejected at commit
  {
    Checker c{rank};
    const bool rejected = g_colorInvalidArguments > 0;
    c.expect(rejected == rejectsColor,
        rejectsColor ? "uncompositable channel.color not rejected at commit "
                       "with ANARI_STATUS_INVALID_ARGUMENT"
                     : "compositable channel.color rejected at commit");
    if (rejectsColor && !c.failures)
      printf("PASS: rank %d: channel.color rejected at commit\n", rank);
    failures += c.failures;
  }

  if (opts.uncommitted) {
    // ANARI parameters take effect on commit: rendering must still use the
    // committed size and color type
    anari::setParameter(d, frame, "size", IMAGE_SIZE / 2u);
    anari::setParameter(d,
        frame,
        "channel.color",
        opts.colorType == ANARI_UFIXED8_VEC4 ? ANARI_UFIXED8_RGBA_SRGB
                                             : ANARI_UFIXED8_VEC4);
  }

  g_callback.rendering = &finalState;
  g_callback.finalFrame = true;
  g_callback.renderFails = rejectsColor || opts.failLastRank;
  g_callback.errorsBeforeRender = g_errors;
  const int errorsBefore = g_colorErrors;
  const int anyErrorsBefore = g_errors;
  renderAndWait(d, frame);
  renders++;

  // Every rank: a render error comes through the status callback (the
  // process is still here, so nothing threw out of anariRenderFrame).
  {
    Checker c{rank};
    const bool wantError = rejectsColor;
    const bool gotError = g_colorErrors > errorsBefore;
    if (opts.failLastRank && !wantError) {
      c.expect(g_errors > anyErrorsBefore,
          "no ERROR reported after the last rank failed to render");
    } else {
      c.expect(wantError == gotError,
          wantError ? "no ERROR about channel.color reported for an "
                      "uncompositable color type"
                    : "unexpected ERROR about channel.color reported");
    }
    if (wantError && !c.failures)
      printf("PASS: rank %d: render error reported\n", rank);
    failures += c.failures;
  }

  if (opts.failLastRank) {
    // nothing was composited, so there is nothing to map
    if (finalLayout.rank == 0) {
      failures +=
          checkColorRefused(d, frame, "after a failure on one rank", true);
    }
  } else if (finalLayout.rank == 0) {
    failures += checkColor(d, frame, opts, finalLayout);
    failures += checkBackground(d, frame, opts, finalLayout);
    failures += opts.depth ? checkDepth(d, frame, finalLayout.size)
                           : checkDepthRefused(d, frame);
    if (opts.ids)
      failures += checkIds(d, frame, finalLayout);
    failures += checkChannelsRefused(d, frame, opts.ids);
  } else
    touchNonRootMaps(d, frame);

  if (opts.callback)
    failures += checkCallbackCalls(d, frame, rank, renders);

  anari::release(d, frame);
  releaseScene(d, scene);
  anari::release(d, d);
  anari::unloadLibrary(lib);
  freeLayout(firstLayout);
  freeLayout(finalLayout);

  int totalFailures = 0;
  MPI_Allreduce(&failures, &totalFailures, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  if (rank == 0) {
    printf(totalFailures ? "SMOKE FAILED (%d)\n" : "SMOKE OK\n", totalFailures);
  }
  if (totalFailures)
    printf("rank %d exiting with failure\n", rank);
  fflush(stdout);
  MPI_Finalize();
  return totalFailures ? 1 : 0;
}
