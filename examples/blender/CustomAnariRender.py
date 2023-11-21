# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import bpy
import array
import gpu
import math
import dataclasses
import copy
from mathutils import Vector
from gpu_extras.presets import draw_texture_2d
from bl_ui.properties_render import RenderButtonsPanel
from bpy.types import Panel
import numpy as np
from anari import *

bl_info = {
    "name": "Custom ANARI renderer",
    "blender": (3, 5, 0),
    "Description": "ANARI renderer integration",
    "category": "Render",
}

prefixes = {
    lib.ANARI_SEVERITY_FATAL_ERROR : "FATAL",
    lib.ANARI_SEVERITY_ERROR : "ERROR",
    lib.ANARI_SEVERITY_WARNING : "WARNING",
    lib.ANARI_SEVERITY_PERFORMANCE_WARNING : "PERFORMANCE",
    lib.ANARI_SEVERITY_INFO : "INFO",
    lib.ANARI_SEVERITY_DEBUG : "DEBUG"
}

renderer_enum_info = [("default", "default", "default")]
anari_in_use = 0

def anari_status(device, source, sourceType, severity, code, message):
    print('[%s]: '%prefixes[severity]+message)

def get_renderer_enum_info(self, context):
    global renderer_enum_info
    return renderer_enum_info

status_handle = ffi.new_handle(anari_status) #something needs to keep this handle alive

class ANARISceneProperties(bpy.types.PropertyGroup):
    debug: bpy.props.BoolProperty(name = "debug", default = False)
    trace: bpy.props.BoolProperty(name = "trace", default = False)

    accumulation: bpy.props.BoolProperty(name = "accumulation", default = False)
    iterations: bpy.props.IntProperty(name = "iterations", default = 8)

    @classmethod
    def register(cls):
        bpy.types.Scene.anari = bpy.props.PointerProperty(
            name="ANARI Scene Settings",
            description="ANARI scene settings",
            type=cls,
        )

    @classmethod
    def unregister(cls):
        del bpy.types.Scene.anari




class RENDER_PT_anari(RenderButtonsPanel, Panel):
    bl_label = "ANARI"
    COMPAT_ENGINES = set()

    @classmethod
    def poll(cls, context):
        return (context.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        global anari_in_use
        col = layout.column()
        col.enabled = anari_in_use == 0
        col.prop(context.scene.anari, 'debug')
        if context.scene.anari.debug:
            col.prop(context.scene.anari, 'trace')

        col = layout.column()
        col.prop(context.scene.anari, 'accumulation')

        col = layout.column()
        col.enabled = context.scene.anari.accumulation
        col.prop(context.scene.anari, 'iterations')



class ANARIRenderEngine(bpy.types.RenderEngine):
    # These three members are used by blender to set up the
    # RenderEngine; define its internal name, visible name and capabilities.
#    bl_idname = "ANARI"
#    bl_label = "Anari"
    bl_use_preview = True
    bl_use_shading_nodes_custom=False

    # Init is called whenever a new render engine instance is created. Multiple
    # instances may exist at the same time, for example for a viewport and final
    # render.
    def __init__(self):
        dummy = gpu.types.GPUFrameBuffer()
        dummy.bind()

        self.scene_data = None
        self.draw_data = None

        global anari_in_use
        anari_in_use = anari_in_use + 1

        self.library = anariLoadLibrary(self.anari_library_name, status_handle)
        if not self.library:
            #if loading the library fails substitute the sink device
            self.library = anariLoadLibrary('sink', status_handle)

        self.device = anariNewDevice(self.library, self.anari_device_name)
        anariCommitParameters(self.device, self.device)

        features = anariGetDeviceExtensions(self.library, "default")

        rendererParameters = anariGetObjectInfo(self.device, ANARI_RENDERER, "default", "parameter", ANARI_PARAMETER_LIST)

        if bpy.context.scene.anari.debug:
            nested = self.device
            self.debug = anariLoadLibrary('debug', status_handle)
            self.device = anariNewDevice(self.debug, 'debug')
            anariSetParameter(self.device, self.device, 'wrappedDevice', ANARI_DEVICE, nested)
            if bpy.context.scene.anari.debug:
                anariSetParameter(self.device, self.device, 'traceMode', ANARI_STRING, 'code')
            anariCommitParameters(self.device, self.device)

        self.width = 0
        self.height = 0
        self.frame = None
        self.camera = None
        self.camera_data = None
        self.perspective = anariNewCamera(self.device, 'perspective')
        self.ortho = anariNewCamera(self.device, 'orthographic')
        self.world = anariNewWorld(self.device)
        self.current_renderer = "default"
        self.renderer = anariNewRenderer(self.device, self.current_renderer)
        anariCommitParameters(self.device, self.renderer)

        self.default_material = anariNewMaterial(self.device, 'matte')
        anariCommitParameters(self.device, self.default_material)

        self.meshes = dict()
        self.lights = dict()
        self.arrays = dict()
        self.images = dict()
        self.samplers = dict()
        self.materials = dict()

        self.scene_instances = []

        self.gputexture = None
        self.rendering = False
        self.scene_updated = False
        self.iteration = 0
        self.epoch = 0
        self.rendered_epoch = 0

    # When the render engine instance is destroyed, this is called. Clean up any
    # render engine data here, for example stopping running render threads.
    def __del__(self):
        global anari_in_use
        anari_in_use = anari_in_use - 1

    def scene_changed(self):
        self.scene_updated = True
        self.iteration = 0
        self.epoch += 1

    def render_converged(self):
        threshold = 1
        if bpy.context.scene.anari.accumulation:
           threshold = bpy.context.scene.anari.iterations

        return self.iteration >= threshold

    def anari_frame(self, width=0, height=0):
        if not self.frame:
            self.frame = anariNewFrame(self.device)
            anariSetParameter(self.device, self.frame, 'channel.color', ANARI_DATA_TYPE, ANARI_UFIXED8_VEC4)

        if width != 0 and height != 0:
            anariSetParameter(self.device, self.frame, 'size', ANARI_UINT32_VEC2, [width, height])

        return self.frame

    scene_data = dict()

    def project_bounds(self, origin, axis):
        values = (1e20, -1.e20)
        for i in range(0, 8):
            corner = Vector((
                self.bounds_min[0] if (i&1)==0 else self.bounds_max[0],
                self.bounds_min[1] if (i&2)==0 else self.bounds_max[1],
                self.bounds_min[2] if (i&4)==0 else self.bounds_max[2],
            ))-origin
            v = corner.dot(axis)
            values = (min(values[0], v), max(values[1], v))
        return values

    def extract_camera(self, depsgraph, width, height, region_view=None, space_view=None):
        scene = depsgraph.scene

        camera = None
        transform = None
        fovx = 1
        zoom = 1

        if region_view:
            zoom = 4 / ((math.sqrt(2) + region_view.view_camera_zoom / 50) ** 2)
            if region_view.view_perspective == 'CAMERA':
                if space_view and space_view.use_local_camera:
                    camera = region_view.camera
                else:
                    camera = scene.objects['Camera']
        else:
            camera = scene.objects['Camera']

        if camera:
            transform = camera.matrix_world
            if camera.data.type == "ORTHO":
                fovx = 0
                zoom = camera.data.ortho_scale
            else:
                fovx = camera.data.angle_x
                zoom = 1
        else:
            transform = region_view.view_matrix.inverted()
            if region_view.view_perspective == "ORTHO":
                fovx = 0
                zoom = 1.0275 * region_view.view_distance * 35 / space_view.lens
            else:
                fovx = 2.0 * math.atan(36 / space_view.lens)
                zoom = 1

        cam_transform = transform.transposed()
        cam_pos = cam_transform[3][0:3]
        cam_view = (-cam_transform[2])[0:3]
        cam_up = cam_transform[1][0:3]

        camera_data = (fovx, zoom) + tuple(cam_pos) + tuple(cam_view) + tuple(cam_up)

        # only updated if the camera actually changed
        if camera_data == self.camera_data:
            return None
        else:
            self.camera_data = camera_data

        if fovx > 0:
            self.camera = self.perspective
            fovy = 2.0*math.atan(math.tan(0.5*fovx)/width*height*zoom)
            anariSetParameter(self.device, self.camera, 'fovy', ANARI_FLOAT32, fovy)
        else:
            bounds = self.project_bounds(Vector(cam_pos), Vector(cam_view))
            cam_pos = (Vector(cam_pos) + bounds[0]*Vector(cam_view))[:]
            self.camera = self.ortho
            anariSetParameter(self.device, self.camera, 'height', ANARI_FLOAT32, 2.0*zoom/width*height)


        anariSetParameter(self.device, self.camera, 'aspect', ANARI_FLOAT32, width/height)
        anariSetParameter(self.device, self.camera, 'position', ANARI_FLOAT32_VEC3, cam_pos)
        anariSetParameter(self.device, self.camera, 'direction', ANARI_FLOAT32_VEC3, cam_view)
        anariSetParameter(self.device, self.camera, 'up', ANARI_FLOAT32_VEC3, cam_up)

        anariCommitParameters(self.device, self.camera)

        return self.camera

    def get_mesh(self, name):
        if name in self.meshes:
            return self.meshes[name]
        else:
            mesh = anariNewGeometry(self.device, 'triangle')
            surface = anariNewSurface(self.device)
            group = anariNewGroup(self.device)
            instance = anariNewInstance(self.device, "transform")
            material = self.default_material

            anariSetParameter(self.device, surface, 'geometry', ANARI_GEOMETRY, mesh)
            anariSetParameter(self.device, surface, 'material', ANARI_MATERIAL, material)
            anariCommitParameters(self.device, surface)

            surfaces = ffi.new('ANARISurface[]', [surface])
            array = anariNewArray1D(self.device, surfaces, ANARI_SURFACE, 1)
            anariSetParameter(self.device, group, 'surface', ANARI_ARRAY1D, array)
            anariCommitParameters(self.device, group)

            anariSetParameter(self.device, instance, 'group', ANARI_GROUP, group)
            anariCommitParameters(self.device, instance)

            result = (mesh, material, surface, group, instance)
            self.meshes[name] = result
            return result


    def get_light(self, name, subtype):
        if name in self.lights and self.lights[name][1] == subtype:
            return self.lights[name][0]
        else:
            light = anariNewLight(self.device, subtype)
            self.lights[name] = (light, subtype)
            return light


    def set_array(self, obj, name, atype, count, arr):
        (ptr, stride) = anariMapParameterArray1D(self.device, obj, name, atype, count)
        ffi.memmove(ptr, ffi.from_buffer(arr), arr.size*arr.itemsize)
        anariUnmapParameterArray(self.device, obj, name)


    def mesh_to_geometry(self, objmesh, name, mesh):
        objmesh.calc_loop_triangles()
        objmesh.calc_normals_split()

        indexcount = len(objmesh.loop_triangles)
        npindex = np.zeros([indexcount*3], dtype=np.uint32)
        objmesh.loop_triangles.foreach_get('vertices', npindex)

        loopindexcount = len(objmesh.loop_triangles)
        nploopindex = np.zeros([loopindexcount*3], dtype=np.uint32)
        objmesh.loop_triangles.foreach_get('loops', nploopindex)

        flatten = not np.array_equal(npindex, nploopindex)

        if not flatten:
            self.set_array(mesh, 'primitive.index', ANARI_UINT32_VEC3, indexcount, npindex)

        vertexcount = len(objmesh.vertices)
        npvert = np.zeros([vertexcount*3], dtype=np.float32)
        objmesh.vertices.foreach_get('co', npvert)
        if flatten:
            npvert = npvert.reshape((vertexcount, 3))
            npvert = npvert[npindex]
            vertexcount = 3*indexcount
        
        self.set_array(mesh, 'vertex.position', ANARI_FLOAT32_VEC3, vertexcount, npvert)

        normalcount = len(objmesh.loops)
        npnormal = np.zeros([normalcount*3], dtype=np.float32)
        objmesh.loops.foreach_get('normal', npnormal)
        if flatten:
            npnormal = npnormal.reshape((normalcount, 3))
            npnormal = npnormal[nploopindex]
            normalcount = 3*loopindexcount
            
        self.set_array(mesh, 'vertex.normal', ANARI_FLOAT32_VEC3, normalcount, npnormal)

        if objmesh.uv_layers.active:
            uvcount = len(objmesh.uv_layers.active.uv)
            npuv = np.zeros([uvcount*2], dtype=np.float32)
            objmesh.uv_layers.active.uv.foreach_get('vector', npuv)
            if flatten:
                npuv = npuv.reshape((uvcount, 2))
                npuv = npuv[nploopindex]
                uvcount = 3*loopindexcount

            self.set_array(mesh, 'vertex.attribute0', ANARI_FLOAT32_VEC2, uvcount, npuv)


        if objmesh.color_attributes:
            colorcount = len(objmesh.color_attributes[0].data)
            npcolor = np.zeros([colorcount*4], dtype=np.float32)
            objmesh.color_attributes[0].data.foreach_get('color', npcolor)
            if flatten:
                npcolor = npcolor.reshape((colorcount, 4))
                npcolor = npcolor[nploopindex]
                colorcount = 3*loopindexcount

            self.set_array(mesh, 'vertex.color', ANARI_FLOAT32_VEC4, colorcount, npcolor)

        anariCommitParameters(self.device, mesh)

        return mesh

    def image_handle(self, image):
        if image.name in self.images:
            return self.images[image.name]
        else:
            image.update()
            if image.has_data:
                atype = None
                #pixbuf = np.array(image.pixels, dtype=np.float32)
                pixbuf = np.empty((len(image.pixels)), dtype=np.float32)
                image.pixels.foreach_get(pixbuf)
                if image.depth//image.channels <= 8:
                    atype = [ANARI_UFIXED8, ANARI_UFIXED8_VEC2, ANARI_UFIXED8_VEC3, ANARI_UFIXED8_VEC4][image.channels-1]
                    pixbuf = (pixbuf*255).astype(np.ubyte)
                else:
                    atype = [ANARI_FLOAT32, ANARI_FLOAT32_VEC2, ANARI_FLOAT32_VEC3, ANARI_FLOAT32_VEC4][image.channels-1]

                pixels = anariNewArray2D(self.device, ffi.from_buffer(pixbuf), atype, image.size[0], image.size[1])
                self.images[image.name] = pixels
                return pixels
            else:
                return None

    def sampler_handle(self, material, paramname):
        key = (material, paramname)
        if key in self.samplers:
            return self.samplers[key]
        else:
            sampler = anariNewSampler(self.device, "image2D")
            self.samplers[key] = sampler
            return sampler

    def parse_source_node(self, material, paramname, atype, input):
        if input.links:
            link = input.links[0]
            node = link.from_node
            if node.type == 'VERTEX_COLOR':
                anariSetParameter(self.device, material, paramname, ANARI_STRING, 'color')
                return
            elif node.type == 'TEX_IMAGE' and node.image:
                image = node.image
                pixels = self.image_handle(image)
                if pixels:
                    sampler = self.sampler_handle(material, paramname)
                    anariSetParameter(self.device, sampler, 'image', ANARI_ARRAY2D, pixels)
                    anariSetParameter(self.device, sampler, 'inAttribute', ANARI_STRING, "attribute0")
                    if link.from_socket.name == "Alpha":
                        #swizzle alpha into first position
                        anariSetParameter(self.device, sampler, 'outTransform', ANARI_FLOAT32_MAT4, [0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0])
                    elif link.from_socket.name == "Color":
                        #make sure we don't accidentally side channel alpha
                        anariSetParameter(self.device, sampler, 'outTransform', ANARI_FLOAT32_MAT4, [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0])
                        anariSetParameter(self.device, sampler, 'outOffset', ANARI_FLOAT32_VEC4, [0,0,0,1])

                    repeatMode = None
                    if node.extension == 'REPEAT':
                        repeatMode = 'repeat'
                    elif node.extension == 'MIRROR':
                        repeatMode = 'mirrorRepeat'
                    elif node.extension == 'EXTEND':
                        repeatMode = 'clampToEdge'

                    if repeatMode:
                        anariSetParameter(self.device, sampler, 'wrapMode1', ANARI_STRING, repeatMode)
                        anariSetParameter(self.device, sampler, 'wrapMode2', ANARI_STRING, repeatMode)

                    filterMode = None
                    if node.interpolation == 'LINEAR':
                        filterMode = 'linear'
                    elif node.interpolation == 'CLOSEST':
                        filterMode = 'nearest'

                    if filterMode:
                        anariSetParameter(self.device, sampler, 'filter', ANARI_STRING, filterMode)


                    anariCommitParameters(self.device, sampler)
                    anariSetParameter(self.device, material, paramname, ANARI_SAMPLER, sampler)
                    return
                else:
                    print("no pixels")

        # use the default value if none of the previous ones worked
        if atype == ANARI_FLOAT32:
            anariSetParameter(self.device, material, paramname, atype, input.default_value)
        elif atype == ANARI_FLOAT32_VEC2:
            anariSetParameter(self.device, material, paramname, atype, input.default_value[:2])
        elif atype == ANARI_FLOAT32_VEC3:
            anariSetParameter(self.device, material, paramname, atype, input.default_value[:3])
        elif atype == ANARI_FLOAT32_VEC4:
            anariSetParameter(self.device, material, paramname, atype, input.default_value[:4])
        else:
            anariSetParameter(self.device, material, paramname, atype, input.default_value[:])

    def parse_material(self, material):
        nodes = material.node_tree.nodes

        #look for an output node
        out = None
        for n in nodes:
            if n.type == 'OUTPUT_MATERIAL':
                out = n

        if not out:
            return self.default_material

        shader = out.inputs['Surface'].links[0].from_node
        if shader.type == 'BSDF_PRINCIPLED':
            material = anariNewMaterial(self.device, 'physicallyBased')
            self.parse_source_node(material, 'baseColor', ANARI_FLOAT32_VEC3, shader.inputs['Base Color'])
            self.parse_source_node(material, 'normal', ANARI_FLOAT32_VEC3, shader.inputs['Normal'])
            anariSetParameter(self.device, material, "alphaMode", ANARI_STRING, "blend")
            self.parse_source_node(material, 'opacity', ANARI_FLOAT32, shader.inputs['Alpha'])
            self.parse_source_node(material, 'metallic', ANARI_FLOAT32, shader.inputs['Metallic'])
            self.parse_source_node(material, 'roughness', ANARI_FLOAT32, shader.inputs['Roughness'])
            self.parse_source_node(material, 'emissive', ANARI_FLOAT32_VEC3, shader.inputs['Emission'])
            self.parse_source_node(material, 'specular', ANARI_FLOAT32, shader.inputs['Specular'])
            self.parse_source_node(material, 'specularColor', ANARI_FLOAT32, shader.inputs['Specular Tint'])
            self.parse_source_node(material, 'clearcoat', ANARI_FLOAT32, shader.inputs['Clearcoat'])
            self.parse_source_node(material, 'clearcoatRoughness', ANARI_FLOAT32, shader.inputs['Clearcoat Roughness'])
            self.parse_source_node(material, 'clearcoatNormal', ANARI_FLOAT32_VEC3, shader.inputs['Clearcoat Normal'])
            self.parse_source_node(material, 'sheenColor', ANARI_FLOAT32, shader.inputs['Sheen Tint'])
            self.parse_source_node(material, 'sheenRoughness', ANARI_FLOAT32, shader.inputs['Sheen'])
            self.parse_source_node(material, 'ior', ANARI_FLOAT32, shader.inputs['IOR'])
            anariCommitParameters(self.device, material)
            return material
        elif shader.type == 'BSDF_DIFFUSE':
            material = anariNewMaterial(self.device, 'physicallyBased')
            self.parse_source_node(material, 'baseColor', ANARI_FLOAT32_VEC3, shader.inputs['Color'])
            self.parse_source_node(material, 'normal', ANARI_FLOAT32_VEC3, shader.inputs['Normal'])
            self.parse_source_node(material, 'roughness', ANARI_FLOAT32, shader.inputs['Roughness'])
            anariCommitParameters(self.device, material)
            return material
        elif shader.type == 'BSDF_SPECULAR':
            material = anariNewMaterial(self.device, 'physicallyBased')
            self.parse_source_node(material, 'baseColor', ANARI_FLOAT32_VEC3, shader.inputs['Base Color'])
            self.parse_source_node(material, 'normal', ANARI_FLOAT32_VEC3, shader.inputs['Normal'])
            anariSetParameter(self.device, material, "alphaMode", ANARI_STRING, "blend")
            self.parse_source_node(material, 'occlusion', ANARI_FLOAT32, shader.inputs['Ambient Occlusion'])
            self.parse_source_node(material, 'opacity', ANARI_FLOAT32, shader.inputs['Transparency'])
            self.parse_source_node(material, 'roughness', ANARI_FLOAT32, shader.inputs['Roughness'])

            self.parse_source_node(material, 'emissive', ANARI_FLOAT32_VEC3, shader.inputs['Emissive Color'])
            self.parse_source_node(material, 'specular', ANARI_FLOAT32_VEC3, shader.inputs['Specular'])
            self.parse_source_node(material, 'clearcoat', ANARI_FLOAT32, shader.inputs['Clear Coat'])
            self.parse_source_node(material, 'clearcoatRoughness', ANARI_FLOAT32, shader.inputs['Clear Coat Roughness'])
            self.parse_source_node(material, 'clearcoatNormal', ANARI_FLOAT32_VEC3, shader.inputs['Clear Coat Normal'])
            anariCommitParameters(self.device, material)
            return material
        else:
            return self.default_material


    def read_meshes(self, depsgraph):

        for update in depsgraph.updates:
            obj = update.id
            name = obj.name

            if isinstance(obj, bpy.types.Scene):
                continue
            elif isinstance(obj, bpy.types.Material):
                meshname = self.materials[name]
                (mesh, material, surface, group, instance) = self.meshes[meshname]
                material = self.parse_material(obj)
                anariSetParameter(self.device, surface, 'material', ANARI_MATERIAL, material)
                anariCommitParameters(self.device, surface)
                self.meshes[name] = (mesh, material, surface, group, instance)
            elif hasattr(obj, 'type') and obj.type in {'MESH', 'CURVE', 'SURFACE', 'FONT', 'META'}:
                objmesh = obj.to_mesh()
                is_new = name not in self.meshes

                (mesh, material, surface, group, instance) = self.get_mesh(name)

                if is_new or update.is_updated_geometry:
                    self.mesh_to_geometry(objmesh, name, mesh)

                if is_new or update.is_updated_shading:
                    if obj.material_slots:
                        material = self.parse_material(obj.material_slots[0].material)
                        anariSetParameter(self.device, surface, 'material', ANARI_MATERIAL, material)
                        anariCommitParameters(self.device, surface)
                        self.meshes[name] = (mesh, material, surface, group, instance)

                if is_new or update.is_updated_transform:
                    transform = [x for v in obj.matrix_world.transposed() for x in v]
                    anariSetParameter(self.device, instance, 'transform', ANARI_FLOAT32_MAT4, transform)
                    anariCommitParameters(self.device, instance)



        self.bounds_min = np.array([1.e20, 1.e20, 1.e20])
        self.bounds_max = np.array([-1.e20, -1.e20, -1.e20])


        scene_instances = []
        for obj in depsgraph.objects:
            name = obj.name

            if name not in self.meshes:
                if obj.type in {'MESH', 'CURVE', 'SURFACE', 'FONT', 'META'}:
                    objmesh = obj.to_mesh()
                else:
                    continue

                (mesh, material, surface, group, instance) = self.get_mesh(name)
                self.mesh_to_geometry(objmesh, name, mesh)

                if obj.material_slots:
                    objmaterial = obj.material_slots[0].material
                    material = self.parse_material(objmaterial)
                    anariSetParameter(self.device, surface, 'material', ANARI_MATERIAL, material)
                    anariCommitParameters(self.device, surface)
                    self.materials[objmaterial.name] = name
                    self.meshes[name] = (mesh, material, surface, group, instance)
            
                transform = [x for v in obj.matrix_world.transposed() for x in v]
                anariSetParameter(self.device, instance, 'transform', ANARI_FLOAT32_MAT4, transform)
                anariCommitParameters(self.device, instance)

            for v in obj.bound_box[:]:
                corner = obj.matrix_world@Vector(v)
                val = np.array(corner[:])
                self.bounds_min = np.minimum(self.bounds_min, val)
                self.bounds_max = np.maximum(self.bounds_max, val)

            (mesh, material, surface, group, instance) = self.get_mesh(name)

            scene_instances.append(instance)

        (ptr, stride) = anariMapParameterArray1D(self.device, self.world, 'instance', ANARI_INSTANCE, len(scene_instances))
        for i, instance in enumerate(scene_instances):
            ptr[i] = instance
        anariUnmapParameterArray(self.device, self.world, 'instance')
        anariCommitParameters(self.device, self.world)


    def read_lights(self, depsgraph):
        scene_lights = []

        for key, obj in depsgraph.objects.items():
            if obj.hide_render or obj.type != 'LIGHT':
                continue

            name = obj.name

            transform = obj.matrix_world.transposed()
            direction = (-transform[2])[0:3]
            position = transform[3][0:3]

            if obj.data.type == 'POINT':
                light = self.get_light(name, 'point')
                anariSetParameter(self.device, light, 'color', ANARI_FLOAT32_VEC3, obj.data.color[:])
                #blender uses total watts while anari uses watts/sr
                anariSetParameter(self.device, light, 'intensity', ANARI_FLOAT32, obj.data.energy/(4.0*math.pi))
                anariSetParameter(self.device, light, 'position', ANARI_FLOAT32_VEC3, position)
                anariCommitParameters(self.device, light)
                scene_lights.append(light)
            elif obj.data.type == 'SUN':
                light = self.get_light(name, 'directional')
                anariSetParameter(self.device, light, 'color', ANARI_FLOAT32_VEC3, obj.data.color[:])
                anariSetParameter(self.device, light, 'irradiance', ANARI_FLOAT32, obj.data.energy)
                anariSetParameter(self.device, light, 'direction', ANARI_FLOAT32_VEC3, direction)
                anariCommitParameters(self.device, light)
                scene_lights.append(light)


        (ptr, stride) = anariMapParameterArray1D(self.device, self.world, 'light', ANARI_LIGHT, len(scene_lights))
        for i, light in enumerate(scene_lights):
            ptr[i] = light
        anariUnmapParameterArray(self.device, self.world, 'light')
        anariCommitParameters(self.device, self.world)


    def read_scene(self, depsgraph):
        renderer_params = getattr(depsgraph.scene.anari, self.anari_library_name)

        if self.current_renderer != renderer_params.renderer:
            self.renderer = anariNewRenderer(self.device, renderer_params.renderer)
            self.current_renderer = renderer_params.renderer

        params = self.param_selections[self.current_renderer]

        for p, t in params.items():
            if hasattr(renderer_params, p):
                v = getattr(renderer_params, p)
                if t == ANARI_FLOAT32_VEC3:
                    v = v[:]
                anariSetParameter(self.device, self.renderer, p, t, v)

        bg_color = depsgraph.scene.world.color[:]+(1.0,)
        anariSetParameter(self.device, self.renderer, 'background', ANARI_FLOAT32_VEC4, bg_color)

        anariCommitParameters(self.device, self.renderer)



        self.read_meshes(depsgraph)
        self.read_lights(depsgraph)


    # This is the method called by Blender for both final renders (F12) and
    # small preview for materials, world and lights.
    def render(self, depsgraph):
        scene = depsgraph.scene
        scale = scene.render.resolution_percentage / 100.0
        width = int(scene.render.resolution_x * scale)
        height = int(scene.render.resolution_y * scale)

        self.extract_camera(depsgraph, width, height)
        self.read_scene(depsgraph)

        frame = self.anari_frame(width, height)
        anariSetParameter(self.device, frame, 'renderer', ANARI_RENDERER, self.renderer)
        anariSetParameter(self.device, frame, 'camera', ANARI_CAMERA, self.camera)
        anariSetParameter(self.device, frame, 'world', ANARI_WORLD, self.world)
        anariCommitParameters(self.device, frame)

        anariRenderFrame(self.device, frame)
        anariFrameReady(self.device, frame, ANARI_WAIT)
        void_pixels, frame_width, frame_height, frame_type = anariMapFrame(self.device, frame, 'channel.color')

        unpacked_pixels = ffi.buffer(void_pixels, frame_width*frame_height*4)
        pixels = np.array(unpacked_pixels).astype(np.float32)*(1.0/255.0)
        rect = pixels.reshape((width*height, 4))
        anariUnmapFrame(self.device, frame, 'channel.color')

        # Here we write the pixel values to the RenderResult
        result = self.begin_result(0, 0, width, height)
        layer = result.layers[0].passes["Combined"]
        layer.rect = rect
        self.end_result(result)



    # For viewport renders, this method gets called once at the start and
    # whenever the scene or 3D viewport changes. This method is where data
    # should be read from Blender in the same thread.
    def view_update(self, context, depsgraph):
        region = context.region
        scene = depsgraph.scene

        width = region.width
        height = region.height

        self.read_scene(depsgraph)

        frame = self.anari_frame(width, height)
        self.extract_camera(depsgraph, width, height, region_view=context.region_data, space_view=context.space_data)

        anariSetParameter(self.device, frame, 'renderer', ANARI_RENDERER, self.renderer)
        anariSetParameter(self.device, frame, 'camera', ANARI_CAMERA, self.camera)
        anariSetParameter(self.device, frame, 'world', ANARI_WORLD, self.world)
        anariCommitParameters(self.device, frame)

        self.scene_changed()



    # For viewport renders, this method is called whenever Blender redraws
    # the 3D viewport.
    def view_draw(self, context, depsgraph):

        #pull some data
        region = context.region
        scene = depsgraph.scene

        # see if something about the viewport or camera has changed
        if self.width != region.width or self.height != region.height:
            self.width = region.width
            self.height = region.height
            self.scene_changed()

        frame = self.anari_frame(self.width, self.height)

        if self.extract_camera(depsgraph, self.width, self.height, region_view=context.region_data, space_view=context.space_data):
            anariSetParameter(self.device, frame, 'camera', ANARI_CAMERA, self.camera)
            anariCommitParameters(self.device, frame)
            self.scene_changed()


        require_redraw = self.scene_updated
        self.scene_updated = False

        # hard render if we don't have a preexisting image
        if not self.gputexture:
            anariRenderFrame(self.device, frame)
            anariFrameReady(self.device, frame, ANARI_WAIT)
            self.rendering = True
            require_redraw = False

        # pull new data if a render has completed
        if self.rendering and anariFrameReady(self.device, frame, ANARI_NO_WAIT):
            void_pixels, frame_width, frame_height, frame_type = anariMapFrame(self.device, frame, 'channel.color')
            unpacked_pixels = ffi.buffer(void_pixels, frame_width*frame_height*4)
            pixels = np.array(unpacked_pixels).astype(np.float32)*(1.0/255.0)
            anariUnmapFrame(self.device, frame, 'channel.color')

            self.gpupixels = gpu.types.Buffer('FLOAT', frame_width * frame_height * 4, pixels)
            self.gputexture = gpu.types.GPUTexture((frame_width, frame_height), format='RGBA16F', data=self.gpupixels)

            self.rendering = False
            self.iteration += 1

        if not self.render_converged():
            require_redraw = True

        if self.rendered_epoch < self.epoch:
            require_redraw = True

        # present
        gpu.state.blend_set('ALPHA_PREMULT')
        self.bind_display_space_shader(scene)
        draw_texture_2d(self.gputexture, (0, 0), region.width, region.height)
        self.unbind_display_space_shader()
        gpu.state.blend_set('NONE')


        # continue drawing as long as changes happen
        if require_redraw and not self.rendering:
            anariRenderFrame(self.device, frame)
            self.rendered_epoch = self.epoch
            self.rendering = True

        # make blender call us again while we wait in the render to complete
        if self.rendering:
            self.tag_redraw()




# RenderEngines also need to tell UI Panels that they are compatible with.
def get_panels():
    exclude_panels = {
        'VIEWLAYER_PT_filter',
        'VIEWLAYER_PT_layer_passes',
        'RENDER_PT_eevee_ambient_occlusion',
        'RENDER_PT_eevee_motion_blur',
        'RENDER_PT_eevee_next_motion_blur',
        'RENDER_PT_motion_blur_curve',
        'RENDER_PT_eevee_depth_of_field',
        'RENDER_PT_eevee_next_depth_of_field',
        'RENDER_PT_eevee_bloom',
        'RENDER_PT_eevee_volumetric',
        'RENDER_PT_eevee_volumetric_lighting',
        'RENDER_PT_eevee_volumetric_shadows',
        'RENDER_PT_eevee_subsurface_scattering',
        'RENDER_PT_eevee_screen_space_reflections',
        'RENDER_PT_eevee_shadows',
        'RENDER_PT_eevee_next_shadows',
        'RENDER_PT_eevee_sampling',
        'RENDER_PT_eevee_indirect_lighting',
        'RENDER_PT_eevee_indirect_lighting_display',
        'RENDER_PT_eevee_film',
        'RENDER_PT_eevee_hair',
        'RENDER_PT_eevee_performance',
    }
    panels = []
    for panel in bpy.types.Panel.__subclasses__():
        if hasattr(panel, 'COMPAT_ENGINES') and ('BLENDER_RENDER' in panel.COMPAT_ENGINES or 'BLENDER_EEVEE' in panel.COMPAT_ENGINES):
            if panel.__name__ not in exclude_panels:
                panels.append(panel)

    return panels


librarynames = ['helide', 'visrtx', 'visgl']

classes = [
    ANARISceneProperties,
    RENDER_PT_anari
]

panel_names = []


def anari_type_to_property(device, objtype, subtype, paramname, atype):
    value = anariGetParameterInfo(device, objtype, subtype, paramname, atype, "default", atype)
    if atype == ANARI_BOOL:
        if value:
            return bpy.props.BoolProperty(name = paramname, default = value)
        else:
            return bpy.props.BoolProperty(name = paramname)
    elif atype == ANARI_INT32:
        if value:
            return bpy.props.IntProperty(name = paramname, default = value)
        else:
            return bpy.props.IntProperty(name = paramname)
    elif atype == ANARI_FLOAT32:
        if value:
            return bpy.props.FloatProperty(name = paramname, default = value)
        else:
            return bpy.props.FloatProperty(name = paramname)
    elif atype == ANARI_FLOAT32_VEC3:
        if value:
            return bpy.props.FloatVectorProperty(name = paramname, subtype='COLOR', default = value)
        else:
            return bpy.props.FloatVectorProperty(name = paramname, subtype='COLOR')
    elif atype == ANARI_STRING:
        selection = anariGetParameterInfo(device, objtype, subtype, paramname, atype, "value", ANARI_STRING_LIST)
        if selection:
            return bpy.props.EnumProperty(
                name=paramname,
                items = {(s, s, '') for s in selection},
                default = value
            )
        else:
            if value:
                return bpy.props.StringProperty(name = paramname, default = value)
            else:
                return bpy.props.StringProperty(name = paramname)
    else:
        return None

def device_to_propertygroup(engine, idname, scenename, device, objtype, subtype):
    parameters = anariGetObjectInfo(device, objtype, subtype, "parameter", ANARI_PARAMETER_LIST)
    properties = []
    names = {'name'} # exclude name from the displayed options
    for paramname, atype in parameters:
        if paramname in names:
            continue
        else:
            names.add(paramname)

        prop = anari_type_to_property(device, objtype, subtype, paramname, atype)
        if prop:
            properties.append((paramname, prop))

    renderers = anariGetObjectSubtypes(device, ANARI_RENDERER)
    properties.append(('renderer', bpy.props.EnumProperty(
        name='renderer',
        items = {(s, s, 'the %s renderer'%s) for s in renderers},
        default = renderers[0])
    ))

    param_selections = dict()
    for renderer_name in renderers:
        renderer_parameters = anariGetObjectInfo(device, ANARI_RENDERER, renderer_name, "parameter", ANARI_PARAMETER_LIST)
        param_selections[renderer_name] = {x[0]:x[1] for x in renderer_parameters}
        for paramname, atype in renderer_parameters:
            if paramname in names:
                continue
            else:
                names.add(paramname)
            prop = anari_type_to_property(device, ANARI_RENDERER, renderer_name, paramname, atype)
            if prop:
                properties.append((paramname, prop))

    @classmethod
    def register(cls):
        setattr(ANARISceneProperties, scenename, bpy.props.PointerProperty(
            name="ANARI %s Scene Settings"%scenename,
            description="ANARI %s scene settings"%scenename,
            type=cls,
        ))

    @classmethod
    def unregister(cls):
        delattr(ANARISceneProperties, scenename)

    property_group = dataclasses.make_dataclass(
        idname+'_properties',
        properties,
        bases=(bpy.types.PropertyGroup,),
        namespace={
            'register':register,
            'unregister':unregister,
        })


    @classmethod
    def poll(cls, context):
        return (context.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        col = layout.column()
        props = getattr(context.scene.anari, self.scenename)
        current_renderer = props.renderer
        selection = self.param_selections[current_renderer]
        col.prop(props, "renderer")
        for paramname, prop in self.panel_props:
            if paramname == "renderer":
                continue
            row = col.row()
            row.enabled = paramname in selection or paramname in self.device_params
            row.prop(props, paramname)

    RENDER_PT_anari_device = type('RENDER_PT_%s_panel'%idname, (RenderButtonsPanel, Panel),{
        'bl_idname': 'RENDER_PT_%s_panel'%idname,
        'bl_label': "%s Device Properties"%idname,
        'bl_parent_id' : "RENDER_PT_anari",
        'COMPAT_ENGINES' : {engine},
        'panel_props' : properties,
        'poll' : poll,
        'draw' : draw,
        'device_params' : names,
        'param_selections' : param_selections,
        'scenename' : copy.copy(scenename)
    })

    return (property_group, RENDER_PT_anari_device, param_selections)

def register():
    # Register the RenderEngine
    for c in classes:
        bpy.utils.register_class(c)

    for name in librarynames:
        library = anariLoadLibrary(name)
        if library:
            devices = anariGetDeviceSubtypes(library)
            
            for devicename in devices:
                label = "%s %s (Anari)"%(name, devicename)
                idname = "ANARI_%s_%s"%(name, devicename)
                class ANARIDeviceRenderEngine(ANARIRenderEngine):
                    # These three members are used by blender to set up the
                    # RenderEngine; define its internal name, visible name and capabilities.
                    bl_idname = copy.copy(idname)
                    bl_label = copy.copy(label)
                    anari_library_name = copy.copy(name)
                    anari_device_name = copy.copy(devicename)

                    def __init__(self):
                        super().__init__()
                        self.props = device_to_propertygroup(self.bl_idname, self.bl_idname, self.anari_library_name, self.device, ANARI_DEVICE, 'default')
                        self.param_selections = self.props[2]

                        if not hasattr(ANARISceneProperties, self.anari_library_name):
                            bpy.utils.register_class(self.props[0])
                            bpy.utils.register_class(self.props[1])
                            classes.append(self.props[0])
                            classes.append(self.props[1])

                bpy.utils.register_class(ANARIDeviceRenderEngine)
                classes.append(ANARIDeviceRenderEngine)
                panel_names.append(label)

                for panel in get_panels():
                    panel.COMPAT_ENGINES.add(idname)

                RENDER_PT_anari.COMPAT_ENGINES.add(idname)








def unregister():
    for c in classes:
        bpy.utils.unregister_class(c)

    for panel in get_panels():
        for p in panel_names:
            if p in panel.COMPAT_ENGINES:
                panel.COMPAT_ENGINES.remove(p)


if __name__ == "__main__":
    register()
