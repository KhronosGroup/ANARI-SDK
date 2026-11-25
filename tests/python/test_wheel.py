# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""Validation of an installed 'anari' wheel."""

import importlib
import importlib.metadata
import os
import pathlib
import re
import shutil
import subprocess
import sys
import sysconfig
import warnings

import pytest

import anari

from conftest import HEIGHT, WIDTH

PACKAGE_DIR = pathlib.Path(anari.__file__).resolve().parent

if sys.platform == 'win32':
    SHLIB_SUFFIXES = ('.dll', '.pyd')
elif sys.platform == 'darwin':
    SHLIB_SUFFIXES = ('.dylib', '.so')
else:
    SHLIB_SUFFIXES = ('.so',)


def _shared_libraries(root):
    return sorted(p for p in root.rglob('*')
                  if p.is_file() and (p.suffix in SHLIB_SUFFIXES
                                      or re.search(r'\.so(\.\d+)+$', p.name)))


def _load(name):
    library = anari.loadLibrary(name, anari.error_status_handle)
    assert library is not None, name
    return library


def _device_library_name(name):
    if sys.platform == 'win32':
        return 'anari_library_%s.dll' % name
    ext = '.dylib' if sys.platform == 'darwin' else '.so'
    return 'libanari_library_%s%s' % (name, ext)


def test_version_matches_metadata():
    assert anari.__version__ == importlib.metadata.version('anari')
    assert re.fullmatch(re.escape(anari.sdk_version) + r'(\.post\d+)?',
                        anari.__version__), (anari.__version__, anari.sdk_version)


def test_nothing_grafted_by_wheel_repair():
    """auditwheel (anari.libs/) and delocate (.dylibs/) copied nothing in."""
    assert not (PACKAGE_DIR.parent / 'anari.libs').exists()
    assert not (PACKAGE_DIR / '.dylibs').exists()


# The manylinux/musllinux base libraries and Windows' system DLLs
LINUX_SYSTEM_LIBS = re.compile(
    r'^(libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1'
    r'|libstdc\+\+\.so\.6|libgcc_s\.so\.1|ld-linux.*\.so\.\d'
    r'|libc\.musl-.*\.so\.1|ld-musl-.*\.so\.1)$')
WINDOWS_SYSTEM_DLLS = re.compile(
    r'^(kernel32|user32|advapi32|shell32|ole32|ws2_32|bcrypt|ntdll'
    r'|api-ms-win-.*|ucrtbase|python3\d*)\.dll$', re.IGNORECASE)


def _needed(path):
    if sys.platform == 'linux':
        from elftools.elf.elffile import ELFFile
        from elftools.elf.dynamic import DynamicSection
        with open(path, 'rb') as f:
            elf = ELFFile(f)
            for section in elf.iter_sections():
                if isinstance(section, DynamicSection):
                    return [t.needed for t in section.iter_tags()
                            if t.entry.d_tag == 'DT_NEEDED']
        return []
    if sys.platform == 'darwin':
        out = subprocess.run(['otool', '-L', str(path)], check=True,
                             capture_output=True, text=True).stdout
        # Skip the file name, and a dylib's own id
        deps = [l.split(' (')[0].strip() for l in out.splitlines()[1:]]
        return [d for d in deps if not d.endswith('/' + path.name)]
    import pefile
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
    return [e.dll.decode() for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', [])]


def test_dependencies_are_in_wheel_or_system():
    libs = _shared_libraries(PACKAGE_DIR)
    shipped = {p.name.lower() for p in libs}
    assert libs
    problems = []
    for lib in libs:
        for dep in _needed(lib):
            name = dep.rsplit('/', 1)[-1]
            if sys.platform == 'darwin':
                ok = (dep.startswith(('/usr/lib/', '/System/Library/'))
                      or (dep.startswith(('@rpath/', '@loader_path/'))
                          and name.lower() in shipped))
            elif sys.platform == 'win32':
                ok = (name.lower() in shipped
                      or WINDOWS_SYSTEM_DLLS.match(name) is not None)
            else:
                ok = (name.lower() in shipped
                      or LINUX_SYSTEM_LIBS.match(name) is not None)
            if not ok:
                problems.append('%s -> %s' % (lib.relative_to(PACKAGE_DIR), dep))
    assert not problems, 'dependencies outside the wheel:\n' + '\n'.join(problems)


LOAD_ALL_SCRIPT = """
import ctypes, os, sys
package_dir, library_dir, libraries = sys.argv[1], sys.argv[2], sys.argv[3:]
if sys.platform == 'win32':
    # The MSVC runtime and anari.dll
    handles = [os.add_dll_directory(package_dir),
               os.add_dll_directory(library_dir)]
    mode = 0
else:
    mode = os.RTLD_NOW | os.RTLD_LOCAL
for library in libraries:
    ctypes.CDLL(library, mode=mode)
"""


def _assert_loads_with_all_symbols_bound(libraries):
    # In a subprocess, so nothing stays loaded for later tests
    result = subprocess.run(
        [sys.executable, '-c', LOAD_ALL_SCRIPT, str(PACKAGE_DIR),
         str(anari.library_dir())] + [str(p) for p in libraries],
        capture_output=True, text=True)
    assert result.returncode == 0, (result.returncode, result.stderr)


def test_every_shared_library_loads_with_all_symbols_bound():
    libraries = _shared_libraries(PACKAGE_DIR)
    assert libraries
    _assert_loads_with_all_symbols_bound(libraries)


def _macos_wheel_tag():
    """e.g. macosx_11_0_arm64 -> ((11, 0), 'arm64')"""
    wheel = importlib.metadata.distribution('anari').read_text('WHEEL')
    tags = re.findall(r'^Tag: .*-macosx_(\d+)_(\d+)_(\w+)$', wheel, re.M)
    assert tags, wheel
    major, minor, arch = tags[0]
    return (int(major), int(minor)), arch


@pytest.mark.skipif(sys.platform != 'darwin', reason='macOS wheels only')
def test_macos_binaries_match_the_wheel_tag():
    """Every binary, static libraries included, matches the tag's
    architecture and minimum macOS."""
    minimum, arch = _macos_wheel_tag()
    binaries = [p for p in PACKAGE_DIR.rglob('*') if p.is_file()
                and p.suffix in ('.so', '.dylib', '.a')]
    assert binaries
    problems = []
    for path in binaries:
        archs = subprocess.run(['lipo', '-archs', str(path)], check=True,
                               capture_output=True, text=True).stdout.split()
        if archs != [arch]:
            problems.append('%s: built for %s' % (path.name, archs))
        commands = subprocess.run(['otool', '-l', str(path)], check=True,
                                  capture_output=True, text=True).stdout
        for version in re.findall(r'\bminos (\d+)\.(\d+)', commands):
            if tuple(map(int, version)) > minimum:
                problems.append('%s: needs macOS %s.%s' % ((path.name,) + version))
    assert not problems, '\n'.join(sorted(set(problems)))


def test_environment_library(monkeypatch):
    monkeypatch.setenv('ANARI_LIBRARY', 'sink')
    library = _load('environment')
    assert 'default' in library.getDeviceSubtypes()


def test_scene_on_sink(render_scene):
    pixels = render_scene(_load('sink').newDevice('default'))
    assert pixels.shape == (HEIGHT, WIDTH, 4)


DEBUG_WRAPPING_FROM_ENVIRONMENT_SCRIPT = """
import sys
sys.path.insert(0, sys.argv[1])
import anari
from conftest import HEIGHT, WIDTH, render_tutorial_scene
debug = anari.loadLibrary('debug', anari.error_status_handle)
pixels = render_tutorial_scene(debug.newDevice('default'))
assert pixels.shape == (HEIGHT, WIDTH, 4), pixels.shape
"""


def test_scene_on_debug_wrapping_from_environment(tmp_path):
    """debug loads sink through libanari; each finds what it ships next to it,
    in a fresh process and from an unrelated working directory."""
    env = dict(os.environ, ANARI_DEBUG_WRAPPED_LIBRARY='sink')
    result = subprocess.run(
        [sys.executable, '-c', DEBUG_WRAPPING_FROM_ENVIRONMENT_SCRIPT,
         str(pathlib.Path(__file__).resolve().parent)],
        capture_output=True, text=True, env=env, cwd=tmp_path)
    assert result.returncode == 0, (result.returncode, result.stdout,
                                    result.stderr)


def _cmake():
    """This environment's cmake (requirements.txt), else one on PATH."""
    cmake = shutil.which('cmake', path=sysconfig.get_path('scripts')) \
        or shutil.which('cmake')
    assert cmake, 'cmake not found: install tests/python/requirements.txt'
    return cmake


@pytest.fixture(scope='session')
def vendor_device(tmp_path_factory, empty_device_source):
    """examples/empty_helium_device built with nothing but the wheel."""
    cmake = _cmake()
    cmake_dir = subprocess.run(
        [sys.executable, '-m', 'anari', '--cmake-dir'],
        check=True, capture_output=True, text=True).stdout.strip()
    build = tmp_path_factory.mktemp('hecore')
    subprocess.run([
        cmake, '-S', str(empty_device_source), '-B', str(build),
        '-DCMAKE_BUILD_TYPE=Release',
        '-Danari_DIR=' + cmake_dir,
        '-DPython3_EXECUTABLE=' + sys.executable,
    ], check=True)
    subprocess.run([cmake, '--build', str(build), '--config', 'Release',
                    '--parallel'], check=True)
    found = list(build.rglob(_device_library_name('hecore')))
    assert found, 'no hecore library built in ' + str(build)
    return found[0]


def test_vendor_device_builds_and_renders(vendor_device, render_scene):
    _assert_loads_with_all_symbols_bound([vendor_device])
    library = _load('hecore,' + str(vendor_device.parent) + os.sep)
    pixels = render_scene(library.newDevice('default'))
    assert pixels.shape[:2] == (HEIGHT, WIDTH)


@pytest.fixture(scope='session')
def device_package(tmp_path_factory, empty_device_source):
    """pip install examples/empty_helium_device, wrapped by device_package/,
    against the installed anari."""
    cmake = _cmake()
    source = tmp_path_factory.mktemp('hecore_package') / 'package'
    shutil.copytree(pathlib.Path(__file__).resolve().parent / 'device_package',
                    source)
    shutil.copytree(empty_device_source, source / 'hecore')
    env = dict(os.environ)
    env['PATH'] = os.path.dirname(cmake) + os.pathsep + env.get('PATH', '')
    subprocess.run([sys.executable, '-m', 'pip', 'install',
                    '--no-build-isolation', '--no-index', str(source)],
                   check=True, env=env)
    yield
    # Windows can't delete the loaded DLL: don't fail a passing session
    result = subprocess.run(
        [sys.executable, '-m', 'pip', 'uninstall', '-y', 'hecore'],
        capture_output=True, text=True)
    if result.returncode != 0:
        warnings.warn('could not uninstall hecore: ' + result.stderr)


def test_device_package_installs_and_renders(device_package, render_scene):
    importlib.invalidate_caches()  # installed after this process started
    import hecore
    location = pathlib.Path(hecore.__file__).resolve().parent
    assert location.parent == PACKAGE_DIR.parent
    assert any(re.match(r'anari\b', r)
               for r in importlib.metadata.requires('hecore'))
    pixels = render_scene(hecore.newDevice())
    assert pixels.shape[:2] == (HEIGHT, WIDTH)
