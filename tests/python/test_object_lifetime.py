# Copyright 2026 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

"""The Python bindings keep a device library loaded while anything made from
it is alive.

Freeing a Library unloads its device library (anariUnloadLibrary), and Python
frees objects in any order, at interpreter exit too. Each scenario runs to
exit in its own process, where the crash it guards against happens.

Needs the 'anari' bindings and the sink and debug devices on the library search
path (an installed ANARI-SDK):

  python -m pytest tests/python/test_object_lifetime.py
"""

import os
import subprocess
import sys

import pytest

anari = pytest.importorskip('anari')

if anari.loadLibrary('sink') is None or anari.loadLibrary('debug') is None:
    pytest.skip('the sink and debug devices are not on the library search path',
                allow_module_level=True)

OBJECTS_OUTLIVE_LIBRARY = """
import sys, anari
library = anari.loadLibrary(sys.argv[1], anari.error_status_handle)
device = library.newDevice('default')
camera = device.newCamera('perspective')
world = device.newWorld()
del library, device
"""

DEVICE_HELD_AS_PARAMETER = """
import sys, anari
sink = anari.loadLibrary('sink', anari.error_status_handle).newDevice('default')
debug = anari.loadLibrary('debug', anari.error_status_handle).newDevice('default')
debug.setParameter('wrappedDevice', anari.DEVICE, sink)
debug.commitParameters()
camera = debug.newCamera('perspective')
del sink
if sys.argv[1:] == ['unset']:
    debug.unsetParameter('wrappedDevice')
    debug.commitParameters()
camera.setParameter('aspect', anari.FLOAT32, 1.5)
camera.commitParameters()
"""


# 'wrapped' only where debug needs it: what debug loads itself stays loaded
@pytest.mark.parametrize('script,args,wrapped', [
    (OBJECTS_OUTLIVE_LIBRARY, ['sink'], None),
    (OBJECTS_OUTLIVE_LIBRARY, ['debug'], 'sink'),
    (DEVICE_HELD_AS_PARAMETER, [], None),
    (DEVICE_HELD_AS_PARAMETER, ['unset'], None),
], ids=['outlive-sink', 'outlive-debug', 'device-parameter',
        'device-parameter-unset'])
def test_objects_keep_their_library_loaded(script, args, wrapped):
    env = dict(os.environ)
    env.pop('ANARI_DEBUG_WRAPPED_LIBRARY', None)
    if wrapped:
        env['ANARI_DEBUG_WRAPPED_LIBRARY'] = wrapped
    result = subprocess.run([sys.executable, '-c', script] + args,
                            capture_output=True, text=True, env=env)
    assert result.returncode == 0, (result.returncode, result.stderr)
