# Copyright 2021-2024 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import sys
import os
import json
import hash_gen
import merge_anari
import argparse
import pathlib

"""static void fillStruct(ANARIExtensions *extensions, const char **list) {

    for(const char *i = *list;i!=NULL;++i) {
        switch(extension_hash(i)) {

        }
    }
}

"""

class ExtensionUtilityGenerator:
    def __init__(self, anari):
        self.anari = anari
        self.extensions = ["ANARI_"+f for f in self.anari["extensions"]]

    def generate_extension_header(self):
        code = "typedef struct {\n"
        for extension in self.extensions:
            code += "   int %s;\n"%extension
        code += "} ANARIExtensions;\n"
        code += "int anariGetDeviceExtensionStruct(ANARIExtensions *extensions, ANARILibrary library, const char *deviceName);\n"
        code += "int anariGetObjectExtensionStruct(ANARIExtensions *extensions, ANARIDevice device, ANARIDataType objectType, const char *objectName);\n"
        code += "int anariGetInstanceExtensionStruct(ANARIExtensions *extensions, ANARIDevice device, ANARIObject object);\n"

        code += "#ifdef ANARI_EXTENSION_UTILITY_IMPL\n"
        code += "#include <string.h>\n"
        code += "static " + hash_gen.gen_hash_function("extension_hash", self.extensions)
        code += "static void fillExtensionStruct(ANARIExtensions *extensions, const char *const *list) {\n"
        code += "    memset(extensions, 0, sizeof(ANARIExtensions));\n"
        code += "    for(const char *const *i = list;*i!=NULL;++i) {\n"
        code += "        switch(extension_hash(*i)) {\n"
        for idx, extension in enumerate(self.extensions):
            code += "            case %d: extensions->%s = 1; break;\n"%(idx, extension)
        code += "            default: break;\n"
        code += "        }\n"
        code += "    }\n"
        code += "}\n"
        code += "int anariGetDeviceExtensionStruct(ANARIExtensions *extensions, ANARILibrary library, const char *deviceName) {\n"
        code += "    const char *const *list = (const char *const *)anariGetDeviceExtensions(library, deviceName);\n"
        code += "    if(list) {\n"
        code += "        fillExtensionStruct(extensions, list);\n"
        code += "        return 0;\n"
        code += "    } else {\n"
        code += "        return 1;\n"
        code += "    }\n"
        code += "}\n"
        code += "int anariGetObjectExtensionStruct(ANARIExtensions *extensions, ANARIDevice device, ANARIDataType objectType, const char *objectName) {\n"
        code += "    const char *const *list = (const char *const *)anariGetObjectInfo(device, objectType, objectName, \"extension\", ANARI_STRING_LIST);\n"
        code += "    if(list) {\n"
        code += "        fillExtensionStruct(extensions, list);\n"
        code += "        return 0;\n"
        code += "    } else {\n"
        code += "        return 1;\n"
        code += "    }\n"
        code += "}\n"
        code += "int anariGetInstanceExtensionStruct(ANARIExtensions *extensions, ANARIDevice device, ANARIObject object) {\n"
        code += "    const char *const *list = NULL;\n"
        code += "    anariGetProperty(device, object, \"extension\", ANARI_STRING_LIST, &list, sizeof(list), ANARI_WAIT);\n"
        code += "    if(list) {\n"
        code += "        fillExtensionStruct(extensions, list);\n"
        code += "        return 0;\n"
        code += "    } else {\n"
        code += "        return 1;\n"
        code += "    }\n"
        code += "}\n"
        code += "#endif\n"
        return code


parser = argparse.ArgumentParser(description="Generate query functions for an ANARI device.")
parser.add_argument("-d", "--device", dest="devicespec", type=open, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", type=pathlib.Path, action="append", help="Path to the core and extension json root.")
parser.add_argument("-n", "--namespace", dest="namespace", help="Namespace for the classes and filenames.")
parser.add_argument("-o", "--output", dest="outdir", type=pathlib.Path, default=pathlib.Path("."), help="Output directory")
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

#generate files
gen = ExtensionUtilityGenerator(device)


def begin_namespaces(args):
    output = ""
    if args.namespace:
        for n in args.namespace.split("::"):
            output += "namespace %s {\n"%n
    return output

def end_namespaces(args):
    output = ""
    if args.namespace:
        for n in args.namespace.split("::"):
            output += "}\n"
    return output


with open(args.outdir/"anari_extension_utility.h", mode='w') as f:
    f.write("// Copyright 2021-2024 The Khronos Group\n")
    f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")

    f.write("#pragma once\n")
    f.write("#include <anari/anari.h>\n")
    f.write(gen.generate_extension_header())

