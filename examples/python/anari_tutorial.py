# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import matplotlib.pyplot as plt
import numpy as np
from anari import *

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

def anari_status(device, source, sourceType, severity, code, message):
    print('[%s]: '%prefixes[severity]+message)

status_handle = ffi.new_handle(anari_status) #something needs to keep this handle alive

debug = anariLoadLibrary('debug', status_handle)
library = anariLoadLibrary('example', status_handle)

nested = anariNewDevice(library, 'default')
anariCommitParameters(nested, nested)

device = anariNewDevice(debug, 'debug')
anariSetParameter(device, device, 'wrappedDevice', ANARI_DEVICE, nested)
anariSetParameter(device, device, 'traceMode', ANARI_STRING, 'code')
anariCommitParameters(device, device)

camera = anariNewCamera(device, 'perspective')
anariSetParameter(device, camera, 'aspect', ANARI_FLOAT32, width/height)
anariSetParameter(device, camera, 'position', ANARI_FLOAT32_VEC3, cam_pos)
anariSetParameter(device, camera, 'direction', ANARI_FLOAT32_VEC3, cam_view)
anariSetParameter(device, camera, 'up', ANARI_FLOAT32_VEC3, cam_up)
anariCommitParameters(device, camera)

world = anariNewWorld(device)

mesh = anariNewGeometry(device, 'triangle')

array = anariNewArray1D(device, ffi.from_buffer(vertex), ANARI_FLOAT32_VEC3, 4)
anariSetParameter(device, mesh, 'vertex.position', ANARI_ARRAY1D, array)

array = anariNewArray1D(device, ffi.from_buffer(color), ANARI_FLOAT32_VEC4, 4)
anariSetParameter(device, mesh, 'vertex.color', ANARI_ARRAY1D, array)

array = anariNewArray1D(device, ffi.from_buffer(index), ANARI_UINT32_VEC3, 2)
anariSetParameter(device, mesh, 'primitive.index', ANARI_ARRAY1D, array)

anariCommitParameters(device, mesh)

material = anariNewMaterial(device, 'matte')
anariCommitParameters(device, material)

surface = anariNewSurface(device)
anariSetParameter(device, surface, 'geometry', ANARI_GEOMETRY, mesh)
anariSetParameter(device, surface, 'material', ANARI_MATERIAL, material)
anariCommitParameters(device, surface)

surfaces = ffi.new('ANARISurface[]', [surface])
array = anariNewArray1D(device, surfaces, ANARI_SURFACE, 1)
anariSetParameter(device, world, 'surface', ANARI_ARRAY1D, array)

light = anariNewLight(device, 'directional')
anariCommitParameters(device, light)

lights = ffi.new('ANARILight[]', [light])
array = anariNewArray1D(device, lights, ANARI_LIGHT, 1)
anariSetParameter(device, world, 'light', ANARI_ARRAY1D, array)

anariCommitParameters(device, world)

renderer = anariNewRenderer(device, 'default')
anariSetParameter(device, renderer, 'background', ANARI_FLOAT32_VEC4, bg_color)
anariCommitParameters(device, renderer)

frame = anariNewFrame(device)
anariSetParameter(device, frame, 'size', ANARI_UINT32_VEC2, [width, height])
anariSetParameter(device, frame, 'channel.color', ANARI_DATA_TYPE, ANARI_UFIXED8_RGBA_SRGB)
anariSetParameter(device, frame, 'renderer', ANARI_RENDERER, renderer)
anariSetParameter(device, frame, 'camera', ANARI_CAMERA, camera)
anariSetParameter(device, frame, 'world', ANARI_WORLD, world)
anariCommitParameters(device, frame)

anariRenderFrame(device, frame)
anariFrameReady(device, frame, ANARI_WAIT)
void_pixels, frame_width, frame_height, frame_type = anariMapFrame(device, frame, 'channel.color')

unpacked_pixels = ffi.buffer(void_pixels, frame_width*frame_height*4)
pixels = np.array(unpacked_pixels).reshape([height, width, 4])
plt.imshow(pixels)
plt.show()
anariUnmapFrame(device, frame, 'channel.color')