# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import bpy
import array
import gpu
import math
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
current_renderer = 0
anari_in_use = 0

def anari_status(device, source, sourceType, severity, code, message):
    print('[%s]: '%prefixes[severity]+message)

def get_renderer_enum_info(self, context):
    global renderer_enum_info
    return renderer_enum_info

def set_current_renderer(self, value):
    global current_renderer
    current_renderer = value

def get_current_renderer(self):
    global current_renderer
    return current_renderer

def get_current_renderer_name():
    global current_renderer
    global renderer_enum_info
    return renderer_enum_info[current_renderer][1]

status_handle = ffi.new_handle(anari_status) #something needs to keep this handle alive

class ANARISceneProperties(bpy.types.PropertyGroup):
    library: bpy.props.StringProperty(name = "library", default = "helide")
    device: bpy.props.StringProperty(name = "device", default = "default")
    debug: bpy.props.BoolProperty(name = "debug", default = False)
    trace: bpy.props.BoolProperty(name = "trace", default = False)
    sync_meshes: bpy.props.BoolProperty(name = "sync meshes", default = True)
    sync_lights: bpy.props.BoolProperty(name = "sync lights", default = True)

    accumulation: bpy.props.BoolProperty(name = "accumulation", default = False)
    iterations: bpy.props.IntProperty(name = "iterations", default = 8)
    ambient_radiance: bpy.props.FloatProperty(name = "ambient intensity", default = 1.0, min = 0.0)
    ambient_color: bpy.props.FloatVectorProperty(name = "ambient color", default = (1.0, 1.0, 1.0), subtype = 'COLOR')
    renderer: bpy.props.EnumProperty(
        items = get_renderer_enum_info,
        name = "subtype",
        default = None,
        set = set_current_renderer,
        get = get_current_renderer
    )

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
    bl_label = "ANARI Device"
    COMPAT_ENGINES = {'ANARI'}

    @classmethod
    def poll(cls, context):
        return (context.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        pass


class RENDER_PT_anari_device(RenderButtonsPanel, Panel):
    bl_label = "Device"
    bl_parent_id = "RENDER_PT_anari"
    COMPAT_ENGINES = {'ANARI'}

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
        col.prop(context.scene.anari, 'library')
        col.prop(context.scene.anari, 'device')
        col.prop(context.scene.anari, 'debug')
        if context.scene.anari.debug:
            col.prop(context.scene.anari, 'trace')

        col = layout.column()
        col.prop(context.scene.anari, 'sync_meshes')
        col.prop(context.scene.anari, 'sync_lights')


class RENDER_PT_anari_renderer(RenderButtonsPanel, Panel):
    bl_label = "Renderer"
    bl_parent_id = "RENDER_PT_anari"
    COMPAT_ENGINES = {'ANARI'}

    @classmethod
    def poll(cls, context):
        return (context.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        col = layout.column()
        col.prop(context.scene.anari, 'renderer')
        col.prop(context.scene.anari, 'accumulation')

        col = layout.column()
        col.enabled = context.scene.anari.accumulation
        col.prop(context.scene.anari, 'iterations')

        col = layout.column()
        col.prop(context.scene.anari, 'ambient_radiance')
        col.prop(context.scene.anari, 'ambient_color')


class ANARIRenderEngine(bpy.types.RenderEngine):
    # These three members are used by blender to set up the
    # RenderEngine; define its internal name, visible name and capabilities.
    bl_idname = "ANARI"
    bl_label = "Anari"
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

        libraryname = bpy.context.scene.anari.library
        devicename = bpy.context.scene.anari.device

        global anari_in_use
        anari_in_use = anari_in_use + 1

        self.library = anariLoadLibrary(libraryname, status_handle)
        if not self.library:
            #if loading the library fails substitute the sink device
            self.library = anariLoadLibrary('sink', status_handle)

        self.device = anariNewDevice(self.library, devicename)
        anariCommitParameters(self.device, self.device)

        features = anariGetDeviceExtensions(self.library, "default")

        rendererNames = anariGetObjectSubtypes(self.device, ANARI_RENDERER)

        rendererParameters = anariGetObjectInfo(self.device, ANARI_RENDERER, "default", "parameter", ANARI_PARAMETER_LIST)
        print(rendererParameters)

        newRendererList = []

        for name in rendererNames:
            newRendererList.append((name, name, name))

        if len(newRendererList) > 1 and newRendererList[0][0] == "default":
            newRendererList.pop(0)

        if len(newRendererList) == 0:
            newRendererList.append(("default", "default", "default"))

        global renderer_enum_info
        renderer_enum_info = newRendererList

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
        self.current_renderer = current_renderer
        self.renderer = anariNewRenderer(self.device, get_current_renderer_name())
        anariCommitParameters(self.device, self.renderer)

        self.default_material = anariNewMaterial(self.device, 'matte')
        anariCommitParameters(self.device, self.default_material)

        self.meshes = dict()
        self.lights = dict()
        self.arrays = dict()
        self.images = dict()
        self.samplers = dict()

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
            anariSetParameter(self.device, material, "alphaMode", ANARI_STRING, "blend")
            self.parse_source_node(material, 'opacity', ANARI_FLOAT32, shader.inputs['Alpha'])
            self.parse_source_node(material, 'metallic', ANARI_FLOAT32, shader.inputs['Metallic'])
            self.parse_source_node(material, 'roughness', ANARI_FLOAT32, shader.inputs['Roughness'])
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
            elif obj.type in {'MESH', 'CURVE', 'SURFACE', 'FONT', 'META'}:
                objmesh = obj.to_mesh()
            else:
                continue

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
                    material = self.parse_material(obj.material_slots[0].material)
                    anariSetParameter(self.device, surface, 'material', ANARI_MATERIAL, material)
                    anariCommitParameters(self.device, surface)
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
        global current_renderer
        if self.current_renderer != current_renderer:
            self.renderer = anariNewRenderer(self.device, get_current_renderer_name())

        bg_color = depsgraph.scene.world.color[:]+(1.0,)
        anariSetParameter(self.device, self.renderer, 'background', ANARI_FLOAT32_VEC4, bg_color)
        aRadiance = bpy.context.scene.anari.ambient_radiance
        anariSetParameter(self.device, self.renderer, 'ambientRadiance', ANARI_FLOAT32, aRadiance)
        aColor = bpy.context.scene.anari.ambient_color
        aColor = (aColor.r, aColor.g, aColor.b)
        anariSetParameter(self.device, self.renderer, 'ambientColor', ANARI_FLOAT32_VEC3, aColor)
        anariCommitParameters(self.device, self.renderer)

        if bpy.context.scene.anari.sync_meshes:
            self.read_meshes(depsgraph)
        if bpy.context.scene.anari.sync_lights:
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


classes = [
    ANARISceneProperties,
    ANARIRenderEngine,
    RENDER_PT_anari,
    RENDER_PT_anari_device,
    RENDER_PT_anari_renderer,
]

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


def register():
    # Register the RenderEngine
    for c in classes:
        bpy.utils.register_class(c)

    for panel in get_panels():
        panel.COMPAT_ENGINES.add('ANARI')


def unregister():
    for c in classes:
        bpy.utils.unregister_class(c)

    for panel in get_panels():
        if 'ANARI' in panel.COMPAT_ENGINES:
            panel.COMPAT_ENGINES.remove('ANARI')


if __name__ == "__main__":
    register()
