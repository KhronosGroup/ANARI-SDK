# Copyright 2021-2024 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import matplotlib.pyplot as plt
import numpy as np
from pyanari import lib, ffi


width = 1024
height = 768

cam_pos = [0.0, 0.0, 0.0]
cam_up = [0.0, 1.0, 0.0]
cam_view = [0.1, 0.0, 1.0]

bg_color = [1,1,1,1]

vertex = np.array([
  -1.0, -1.0, 3.0,
  -1.0,  1.0, 3.0,
   1.0, -1.0, 3.0,
   0.1,  0.1, 0.3
], dtype = np.float32)

color =  np.array([
  0.9, 0.5, 0.5, 1.0,
  0.8, 0.8, 0.8, 1.0,
  0.8, 0.8, 0.8, 1.0,
  0.5, 0.9, 0.5, 1.0
], dtype = np.float32)

index = np.array([
  0, 1, 2,
  1, 2, 3
], dtype = np.uint32)

prefixes = {
    lib.ANARI_SEVERITY_FATAL_ERROR : "FATAL",
    lib.ANARI_SEVERITY_ERROR : "ERROR",
    lib.ANARI_SEVERITY_WARNING : "WARNING",
    lib.ANARI_SEVERITY_PERFORMANCE_WARNING : "PERFORMANCE",
    lib.ANARI_SEVERITY_INFO : "INFO",
    lib.ANARI_SEVERITY_DEBUG : "DEBUG"
}

@ffi.def_extern()
def ANARIStatusCallback_python(usrPtr, device, source, sourceType, severity, code, message):
    print('[%s]: '%prefixes[severity]+ffi.string(message).decode('utf-8'))


debug = lib.anariLoadLibrary(b'debug', lib.ANARIStatusCallback_python, ffi.NULL)
library = lib.anariLoadLibrary(b'helide', lib.ANARIStatusCallback_python, ffi.NULL)

nested = lib.anariNewDevice(library, b'default')
lib.anariCommitParameters(nested, nested)

device = lib.anariNewDevice(debug, b'debug')
lib.anariSetParameter(device, device, b'wrappedDevice', lib.ANARI_DEVICE, ffi.new('ANARIDevice*', nested))
lib.anariSetParameter(device, device, b'traceMode', lib.ANARI_STRING, b'code')
lib.anariCommitParameters(device, device)

camera = lib.anariNewCamera(device, b'perspective')
lib.anariSetParameter(device, camera, b'aspect', lib.ANARI_FLOAT32, ffi.new('float*', width/height))
lib.anariSetParameter(device, camera, b'position', lib.ANARI_FLOAT32_VEC3, ffi.new('float[3]', cam_pos))
lib.anariSetParameter(device, camera, b'direction', lib.ANARI_FLOAT32_VEC3, ffi.new('float[3]', cam_view))
lib.anariSetParameter(device, camera, b'up', lib.ANARI_FLOAT32_VEC3, ffi.new('float[3]', cam_up))
lib.anariCommitParameters(device, camera)

world = lib.anariNewWorld(device)

mesh = lib.anariNewGeometry(device, b'triangle')

array = lib.anariNewArray1D(device, ffi.from_buffer(vertex), ffi.NULL, ffi.NULL, lib.ANARI_FLOAT32_VEC3, 4, 0)
lib.anariCommitParameters(device, array)
lib.anariSetParameter(device, mesh, b'vertex.position', lib.ANARI_ARRAY1D, ffi.new('ANARIArray1D*', array))
lib.anariRelease(device, array)

array = lib.anariNewArray1D(device, ffi.from_buffer(color), ffi.NULL, ffi.NULL, lib.ANARI_FLOAT32_VEC4, 4, 0)
lib.anariCommitParameters(device, array)
lib.anariSetParameter(device, mesh, b'vertex.color', lib.ANARI_ARRAY1D, ffi.new('ANARIArray1D*', array))
lib.anariRelease(device, array)

array = lib.anariNewArray1D(device, ffi.from_buffer(index), ffi.NULL, ffi.NULL, lib.ANARI_UINT32_VEC3, 2, 0)
lib.anariCommitParameters(device, array)
lib.anariSetParameter(device, mesh, b'primitive.index', lib.ANARI_ARRAY1D, ffi.new('ANARIArray1D*', array))
lib.anariRelease(device, array)

lib.anariCommitParameters(device, mesh)

material = lib.anariNewMaterial(device, b'matte')
lib.anariCommitParameters(device, material)

surface = lib.anariNewSurface(device)
lib.anariSetParameter(device, surface, b'geometry', lib.ANARI_GEOMETRY, ffi.new('ANARIGeometry*', mesh))
lib.anariSetParameter(device, surface, b'material', lib.ANARI_MATERIAL, ffi.new('ANARIMaterial*', material))
lib.anariCommitParameters(device, surface)
lib.anariRelease(device, mesh)
lib.anariRelease(device, material)

array = lib.anariNewArray1D(device, ffi.new('void*[]', [surface]), ffi.NULL, ffi.NULL, lib.ANARI_SURFACE, 1, 0)
lib.anariCommitParameters(device, array)
lib.anariSetParameter(device, world, b'surface', lib.ANARI_ARRAY1D, ffi.new('ANARIArray1D*', array))
lib.anariRelease(device, surface)
lib.anariRelease(device, array)

light = lib.anariNewLight(device, b'directional')
lib.anariCommitParameters(device, light)
array = lib.anariNewArray1D(device, ffi.new('void*[]', [light]), ffi.NULL, ffi.NULL, lib.ANARI_LIGHT, 1, 0)
lib.anariCommitParameters(device, array)
lib.anariSetParameter(device, world, b'light', lib.ANARI_ARRAY1D, ffi.new('ANARIArray1D*', array))
lib.anariRelease(device, light)
lib.anariRelease(device, array)

lib.anariCommitParameters(device, world)

renderer = lib.anariNewRenderer(device, b'default')
lib.anariSetParameter(device, renderer, b'background', lib.ANARI_FLOAT32_VEC4, ffi.new('float[4]', bg_color))
lib.anariCommitParameters(device, renderer)

frame = lib.anariNewFrame(device)
lib.anariSetParameter(device, frame, b'size', lib.ANARI_UINT32_VEC2, ffi.new('uint32_t[2]', [width, height]))
lib.anariSetParameter(device, frame, b'channel.color', lib.ANARI_DATA_TYPE, ffi.new('ANARIDataType*', lib.ANARI_UFIXED8_RGBA_SRGB))
lib.anariSetParameter(device, frame, b'renderer', lib.ANARI_RENDERER, ffi.new('ANARIRenderer*', renderer))
lib.anariSetParameter(device, frame, b'camera', lib.ANARI_CAMERA, ffi.new('ANARICamera*', camera))
lib.anariSetParameter(device, frame, b'world', lib.ANARI_WORLD, ffi.new('ANARIWorld*', world))
lib.anariCommitParameters(device, frame)

lib.anariRenderFrame(device, frame)
lib.anariFrameReady(device, frame, lib.ANARI_WAIT)
frame_width = ffi.new('uint32_t*', 0)
frame_height = ffi.new('uint32_t*', 0)
frame_type = ffi.new('ANARIDataType*', 0)
void_pixels = lib.anariMapFrame(device, frame, b'channel.color', frame_width, frame_height, frame_type)

unpacked_pixels = ffi.buffer(void_pixels, width*height*4)
pixels = np.array(unpacked_pixels).reshape([height, width, 4])
plt.imshow(pixels)
plt.show()


lib.anariRelease(device, device)
lib.anariUnloadLibrary(library)