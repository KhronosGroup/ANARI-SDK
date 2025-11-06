# Copyright 2021-2025 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import matplotlib.pyplot as plt
import numpy as np
import helide as backend
import anari

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

device = backend.newDevice()
device.commitParameters()

version = device.getProperty('version.name', anari.STRING, anari.WAIT)
print("version.name =", version)

camera = device.newCamera('perspective')
camera.setParameter('aspect', anari.FLOAT32, width/height)
camera.setParameter('position', anari.FLOAT32_VEC3, cam_pos)
camera.setParameter('direction', anari.FLOAT32_VEC3, cam_view)
camera.setParameter('up', anari.FLOAT32_VEC3, cam_up)
camera.commitParameters()

world = device.newWorld()

mesh = device.newGeometry('triangle')

mesh.setParameterArray1D('vertex.position', anari.FLOAT32_VEC3, vertex)

mesh.setParameterArray1D('vertex.color', anari.FLOAT32_VEC4, color)

mesh.setParameterArray1D('primitive.index', anari.UINT32_VEC3, index)

mesh.commitParameters()

material = device.newMaterial('matte')
material.setParameter('color', anari.STRING, 'color')
material.commitParameters()

surface = device.newSurface()
surface.setParameter('geometry', anari.GEOMETRY, mesh)
surface.setParameter('material', anari.MATERIAL, material)
surface.commitParameters()

world.setParameterArray1D('surface', anari.SURFACE, [surface])

light = device.newLight('directional')
light.setParameter('direction', anari.FLOAT32_VEC3, [0, 0, 1])
light.commitParameters()

world.setParameterArray1D('light', anari.LIGHT, [light])

world.commitParameters()

renderer = device.newRenderer('default')
renderer.setParameter('background', anari.FLOAT32_VEC4, bg_color)
# renderer.setParameter('ambientRadiance', anari.FLOAT32, 0.3)
renderer.commitParameters()

frame = device.newFrame()
frame.setParameter('size', anari.UINT32_VEC2, [width, height])
frame.setParameter('channel.color', anari.DATA_TYPE, anari.FLOAT32_VEC4)
frame.setParameter('renderer', anari.RENDERER, renderer)
frame.setParameter('camera', anari.CAMERA, camera)
frame.setParameter('world', anari.WORLD, world)
frame.commitParameters()

frame.renderFrame()
frame.frameReady(anari.WAIT)
# pixels, frame_width, frame_height, frame_type = frame.mapFrame('channel.color')
# print(pixels)
# unpacked_pixels = anari.ffi.buffer(pixels, frame_width*frame_height*4)
# pixels = np.array(unpacked_pixels).reshape([height, width, 4])
pixels = frame.channelAsNdarray('channel.color')
plt.imshow(pixels)
plt.show()
# frame.unmapFrame('channel.color')