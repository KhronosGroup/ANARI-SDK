# Copyright 2021-2025 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

from cffi import FFI
import sys
import os
import json
import merge_anari
import argparse
import pathlib
import glob

import device_wrapper


parser = argparse.ArgumentParser(description="generate cffi bindings")

parser.add_argument("-n", "--name", dest="name", type=str, help="module name")
parser.add_argument("-d", "--device", dest="devicespec", type=open, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", type=pathlib.Path, action="append", help="Path to the core and extension json root.")
parser.add_argument("-i", "--include", dest="includes", type=str, action="append", help="include")
parser.add_argument("-R", "--forcereleaselink", action="store_true", help="Force the extension to implicitly link to a release python library on Windows, overriding possible debug python linking")
parser.add_argument("-O", "--output", dest="outdir", type=pathlib.Path, help="Output directory")
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

# print(defs)

includes = '#include "anari/anari.h"\n'
if args.includes:
    for i in args.includes:
        includes += '#include "'+i+'"\n'

ffibuilder = FFI()
ffibuilder.cdef(defs)
ffibuilder.set_source(args.name, includes)

bindings_file = args.outdir/'anari_api_bindings.c'
ffibuilder.emit_c_code(str(bindings_file))
if args.forcereleaselink:
    # Add an undef _DEBUG to the file to force compilation in release mode
    with open(bindings_file, 'r') as file:
        existing_content = file.read()
    with open(bindings_file, 'w') as file:
        file.write('#undef _DEBUG' + '\n' + existing_content)

wrapper = device_wrapper.write_wrappers(device, args.name)

with open(args.outdir/'utils.py', mode='w') as f:
    f.write(wrapper)