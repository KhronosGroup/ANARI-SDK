# C++ is a self-contained conformance runner; Python and C++ talk only through files

The revamped CTS makes a C++ command-line tool the core of the suite. It renders
each test *and* evaluates the result against ground-truth images (pass/fail),
writing rendered images plus a machine-readable results manifest to disk. Python
is demoted to a reporting/analysis layer that only reads those files: it builds
PDF reports from manifests and diffs one device's results against another's. The
two languages never share a process — pybind11 is removed entirely.

We chose this over keeping pybind11 (smallest change, but the C++ tool would not
be "the core" and the Python/C++ coupling that makes the current suite confusing
would remain) and over a render-only C++ tool that leaves all comparison in
Python (keeps scikit-image's mature metrics, but "run the tests" would not yield
pass/fail without Python, so the conformance verdict would still straddle two
languages).

Consequences: the image-comparison metrics currently provided by scikit-image
(SSIM, PSNR) must be reimplemented in C++ (or vendored). `stb_image` is already a
dependency of `anari_test_scenes`, so PNG read/write in C++ is available.
