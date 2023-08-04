# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

from cffi import FFI
import sys
import os
import json
import merge_anari
import argparse
import pathlib


parser = argparse.ArgumentParser(description="Generate query functions for an ANARI device.")
parser.add_argument("-d", "--device", dest="devicespec", type=open, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", type=pathlib.Path, action="append", help="Path to the core and extension json root.")
parser.add_argument("-I", "--include", dest="incdirs", type=pathlib.Path, action="append", help="Include directories")
parser.add_argument("-L", "--lib", dest="libdirs", type=pathlib.Path, action="append", help="Library directories")
args = parser.parse_args()


#flattened list of all input jsons in supplied directories
jsons = [entry for j in args.json for entry in j.glob("**/*.json")]

#load the device root
device = json.load(args.devicespec)
merge_anari.tag_extension(device)
print("opened " + device["info"]["type"] + " " + device["info"]["name"])

dependencies = merge_anari.crawl_dependencies(device, jsons)
#merge all dependencies
for x in dependencies:
    matches = [p for p in jsons if p.stem == x]
    for m in matches:
        extension = json.load(open(m))
        merge_anari.tag_extension(extension)
        merge_anari.merge(device, extension)

def write_definitions(anari):
    defs = ''

    # generate enums
    for enum in anari['enums']:
        defs += 'typedef '+enum['baseType']+' '+enum['name']+';\n'
        for value in enum['values']:
            defs += '#define '+value['name']+' '+str(value['value'])+'\n'
        defs += '\n'

    for opaque in anari['opaqueTypes']:
        defs += 'typedef void* '+opaque['name']+';\n'

    defs += '\n'

    # generate structs
    for struct in anari['structs']:
        defs += 'typedef struct {\n'

        if struct['members']:
            for member in struct['members']:
                if 'elements' in member:
                    defs += '  '+member['type']+' '+member['name']+'['+str(member['elements'])+'];\n'
                else:
                    defs += '  '+member['type']+' '+member['name']+';\n'

        defs += '} '+struct['name']+';\n'

    defs += '\n'

    for fun in anari['functionTypedefs']:
        defs += 'typedef '+fun['returnType']+' (*'+fun['name']+')('

        if fun['arguments']:
            arg = fun['arguments'][0]
            defs += arg['type']+' '+arg['name']
            for arg in fun['arguments'][1:]:
                defs += ', '+arg['type']+' '+arg['name']

        defs += ');\n'

    defs += '\n'


    for fun in anari['functions']:
        defs += fun['returnType']+' '+fun['name']+'('

        if fun['arguments']:
            arg = fun['arguments'][0]
            defs += arg['type']+' '+arg['name']
            for arg in fun['arguments'][1:]:
                defs += ', '+arg['type']+' '+arg['name']

        defs += ');\n'


    defs += '\n'


    for fun in anari['functionTypedefs']:
        defs += 'extern "Python" ' + fun['returnType']+' '+fun['name']+'_python('

        if fun['arguments']:
            arg = fun['arguments'][0]
            defs += arg['type']+' '+arg['name']
            for arg in fun['arguments'][1:]:
                defs += ', '+arg['type']+' '+arg['name']

        defs += ');\n'


    return defs

defs = write_definitions(device)

ffibuilder = FFI()
ffibuilder.cdef(defs)
ffibuilder.set_source('pyanari',
'''
#include "anari/anari.h"
''',
    libraries = ['anari'],
    library_dirs = [str(x) for x in args.libdirs],
    include_dirs = [str(x) for x in args.incdirs])

if __name__ == "__main__":
    #ffibuilder.emit_c_code(str(args.outdir/"pyanari.c"))
    ffibuilder.compile(verbose=True, debug=False)


boilerplate = '''# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

from pyanari import lib, ffi

@ffi.def_extern()
def ANARIStatusCallback_python(usrPtr, device, source, sourceType, severity, code, message):
    cb = ffi.from_handle(usrPtr)
    cb(device, source, sourceType, severity, code, ffi.string(message).decode('utf-8'))

def _releaseBoth(device, handle):
    lib.anariRelease(device, handle)
    lib.anariRelease(device, device)

'''

special = {
    'anariSetParameter' :
    '''def anariSetParameter(device, handle, name, atype, value):
    if isinstance(value, str):
        value = value.encode('utf-8')
        lib.anariSetParameter(
            device, handle, name.encode('utf-8'),
            atype, ffi.new('const char[]', value))
    else:
        lib.anariSetParameter(
            device, handle, name.encode('utf-8'),
            atype, ffi.new(_conversions[atype], value))

''',
    'anariLoadLibrary' :
    '''def anariLoadLibrary(name, callback):
    device = lib.anariLoadLibrary(name.encode('utf-8'), lib.ANARIStatusCallback_python, callback)
    return ffi.gc(device, lib.anariUnloadLibrary)

''',
    'anariNewArray1D' :
    '''def anariNewArray1D(device, appMemory, dataType, numItems1):
    result = lib.anariNewArray1D(device, ffi.NULL, ffi.NULL, ffi.NULL, dataType, numItems1, 0)
    ptr = lib.anariMapArray(device, result)
    ffi.memmove(ptr, appMemory, ffi.sizeof(_typeof[dataType])*numItems1)
    lib.anariUnmapArray(device, result)
    lib.anariRetain(device, device)
    return ffi.gc(result, lambda h, d=device: _releaseBoth(d, h))

''',
    'anariNewArray2D' :
    '''def anariNewArray2D(device, appMemory, dataType, numItems1, numItems2):
    result = lib.anariNewArray2D(device, ffi.NULL, ffi.NULL, ffi.NULL, dataType, numItems1, numItems2, 0, 0)
    ptr = lib.anariMapArray(device, result)
    ffi.memmove(ptr, appMemory, ffi.sizeof(_typeof[dataType])*numItems1*numItems2)
    lib.anariUnmapArray(device, result)
    lib.anariRetain(device, device)
    return ffi.gc(result, lambda h, d=device: _releaseBoth(d, h))

''',
    'anariNewArray3D' :
    '''def anariNewArray3D(device, appMemory, dataType, numItems1, numItems2, numItems3):
    result = lib.anariNewArray3D(device, ffi.NULL, ffi.NULL, ffi.NULL, dataType, numItems1, numItems2, numItems3)
    ptr = lib.anariMapArray(device, result)
    ffi.memmove(ptr, appMemory, ffi.sizeof(_typeof[dataType])*numItems1)
    lib.anariUnmapArray(device, result)
    lib.anariRetain(device, device)
    return ffi.gc(result, lambda h, d=device: _releaseBoth(d, h))

''',
    'anariMapFrame' :
    '''def anariMapFrame(device, frame, channel):
    frame_width = ffi.new('uint32_t*', 0)
    frame_height = ffi.new('uint32_t*', 0)
    frame_type = ffi.new('ANARIDataType*', 0)
    result = lib.anariMapFrame(device, frame, channel.encode('utf-8'), frame_width, frame_height, frame_type)
    return (result, int(frame_width[0]), int(frame_height[0]), int(frame_type[0]))

''',
    'anariRelease' : '', # remove these to avoid confusion
    'anariRetain' : '',
    'anariUnloadLibrary' : ''
}

objects = {
    'ANARIDevice',
    'ANARIObject',
    'ANARIArray',
    'ANARIArray1D',
    'ANARIArray2D',
    'ANARIArray3D',
    'ANARICamera',
    'ANARIFrame',
    'ANARIGeometry',
    'ANARIGroup',
    'ANARIInstance',
    'ANARILight',
    'ANARIMaterial',
    'ANARIRenderer',
    'ANARISurface',
    'ANARISampler',
    'ANARISpatialField',
    'ANARIVolume',
    'ANARIWorld'
}

def convert_argument(arg):
    if arg['type'] == 'const char*':
        return arg['name']+'.encode(\'utf-8\')'
    else:
        return arg['name']


def write_wrappers(anari):
    code = boilerplate

    for enum in anari['enums']:
        for value in enum['values']:
            code += value['name']+' = lib.'+value['name']+'\n'
        code += '\n'

    types = next((x for x in anari['enums'] if x['name'] == 'ANARIDataType'), None)

    code += '_conversions = {\n'
    for value in types['values']:
        if value['elements'] == 1:
            code += '    %s : \'%s*\',\n'%(value['name'], value['baseType'])
        else:
            code += '    %s : \'%s[%d]\',\n'%(value['name'], value['baseType'], value['elements'])
    code += '}\n'

    code += '_typeof = {\n'
    for value in types['values']:
        if value['elements'] == 1:
            code += '    %s : \'%s\',\n'%(value['name'], value['baseType'])
        else:
            code += '    %s : \'%s[%d]\',\n'%(value['name'], value['baseType'], value['elements'])
    code += '}\n'


    for fun in anari['functions']:
        if fun['name'] in special:
            code += special[fun['name']]
            continue

        code += 'def '+fun['name']+'('
        if fun['arguments']:
            code += ', '.join([x['name'] for x in fun['arguments']])
        code += '):\n'
        code += '    '

        if fun['returnType'] != 'void':
            code += 'result = '

        code += 'lib.'+fun['name']+'('
        code += ', '.join([convert_argument(x) for x in fun['arguments']])
        code += ')\n'

        if fun['returnType'] == 'ANARIDevice':
            code += '    return ffi.gc(result, lambda d: lib.anariRelease(d, d))\n'
        elif fun['returnType'] in objects:
            code += '    lib.anariRetain(device, device)\n'
            code += '    return ffi.gc(result, lambda h, d=device: _releaseBoth(d, h))\n'
        elif fun['returnType'] != 'void':
            code += '    return result\n'

        code += '\n'

    code += '\n'

    return code

with open('anari.py', mode='w') as f:
    f.write(write_wrappers(device))
