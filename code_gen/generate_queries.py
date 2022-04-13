# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import sys
import os
import json
import hash_gen
import merge_anari
import argparse
import pathlib

class QueryGenerator:
    def __init__(self, anari):
        self.anari = anari
        self.named_objects = dict()
        self.anon_objects = dict()
        self.parameter_dict = dict()
        self.parameter_index = list()
        self.objects = dict()
        type_enums = next(x for x in anari["enums"] if x["name"] == "ANARIDataType")
        self.type_enum_dict = {e["name"]: e for e in type_enums["values"]}

        offset = 0
        for obj in anari["objects"]:
            if not obj["type"] in self.objects:
                self.objects[obj["type"]] = []

            parameter_list = []
            parameter_type_list = []
            for param in obj["parameters"]:
                parameter_list.append(param)
                for t in param["types"]:
                    parameter_type_list.append((param["name"], t))

            func_name = obj["type"]
            length = len(parameter_list)
            name = 0
            param_data = {"parameters": parameter_list, "range": range(offset, offset+length), "parameters_with_types": parameter_type_list}
            if "name" in obj:
                self.objects[obj["type"]].append(obj["name"])
                self.named_objects[(obj["type"],obj["name"])] = param_data
                func_name += "_"+obj["name"]
                name = "\""+obj["name"]+"\""
            else:
                self.anon_objects[obj["type"]] = param_data

            self.parameter_dict.update({(obj["type"], name, parameter_list[i]["name"]): offset+i for i in range(0, length)})
            self.parameter_index.extend([(obj["type"], name, parameter_list[i]["name"]) for i in range(0, length)])
            offset += length

        self.named_types = sorted(list(set([k[0] for k in self.named_objects])))
        self.subtype_list = sorted(list(set([k[1] for k in self.named_objects])))

    def preamble(self):
        return "static " + hash_gen.gen_hash_function("subtype_hash", self.subtype_list)

    def generate_subtype_query(self):
        code = "const char ** query_object_types(ANARIDataType type) {\n"
        code += "   switch(type) {\n"
        for key, value in self.objects.items():
            if value:
                code += "      case %s:\n"%(key)
                code += "      {\n"
                code += "         static const char *%s_subtypes[] = {"%(key)
                code += ", ".join(["\"%s\""%x for x in value])+", 0};\n"
                code += "         return %s_subtypes;\n"%key
                code += "      }\n"
        code += "      default:\n"
        code += "      {\n"
        code += "         static const char *none_subtypes[] = {0};\n"
        code += "         return none_subtypes;\n"
        code += "      }\n"
        code += "   }\n"
        code += "}\n"
        return code

    def generate_parameter_query(self):
        code = ""
        for type_enum in self.named_types:
            subtypes = {key[1]: params for key,params in self.named_objects.items() if key[0] == type_enum}
            code += "static const ANARIParameter * "+type_enum+"_params(const char *subtype) {\n"
            code += "   switch(subtype_hash(subtype)) {\n"
            for key, value in subtypes.items():
                if value["parameters_with_types"]:
                    code += "      case %d:\n"%(self.subtype_list.index(key))
                    code += "      {\n"
                    code += "         static const ANARIParameter %s_params[] = {"%(key)
                    code += ", ".join(["{\"%s\", %s}"%x for x in value["parameters_with_types"]])+", {0, ANARI_UNKNOWN}};\n"
                    code += "         return %s_params;\n"%key
                    code += "      }\n"
            code += "      default:\n"
            code += "      {\n"
            code += "         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};\n"
            code += "         return none;\n"
            code += "      }\n"
            code += "   }\n"
            code += "}\n"

        code += "const ANARIParameter * query_params(ANARIDataType type, const char *subtype) {\n"
        code += "   switch(type) {\n"
        for type_enum in self.named_types:
            code += "      case %s:\n"%(type_enum)
            code += "         return %s_params(subtype);\n"%type_enum

        for type_enum, value in self.anon_objects.items():
                if value["parameters_with_types"]:
                    code += "      case %s:\n"%(type_enum)
                    code += "      {\n"
                    code += "         static const ANARIParameter %s_params[] = {"%(type_enum)
                    code += ", ".join(["{\"%s\", %s}"%x for x in value["parameters_with_types"]])+", {0, ANARI_UNKNOWN}};\n"
                    code += "         return %s_params;\n"%type_enum
                    code += "      }\n"
        code += "      default:\n"
        code += "      {\n"
        code += "         static const ANARIParameter none[] = {{0, ANARI_UNKNOWN}};\n"
        code += "         return none;\n"
        code += "      }\n"
        code += "   }\n"
        code += "}\n"
        return code


    def generate_parameter_index(self):
        code = ""

        for type_enum in self.named_types:
            subtypes = {key[1]: params for key,params in self.named_objects.items() if key[0] == type_enum}

            for key, value in subtypes.items():
                if value["parameters"]:
                    parameters = [x["name"] for x in value["parameters"]]
                    code += "static " + hash_gen.gen_hash_function("%s_%s_hash"%(type_enum, key), parameters, value["range"])
                else:
                    code += "static int %s_%s_hash(const char *param) {\n"%(type_enum, key)
                    code += "   return -1;\n"
                    code += "}\n"

            code += "static int "+type_enum+"_index(const char *subtype, const char *param) {\n"
            code += "   switch(subtype_hash(subtype)) {\n"
            for key, value in subtypes.items():
                if value:
                    code += "      case %d:\n"%(self.subtype_list.index(key))
                    code += "         return %s_%s_hash(param);\n"%(type_enum, key)
            code += "      default:\n"
            code += "         return -1;\n"
            code += "   }\n"
            code += "}\n"

        for key, value in self.anon_objects.items():
            if value["parameters"]:
                parameters = [x["name"] for x in value["parameters"]]
                code += "static " + hash_gen.gen_hash_function("%s_hash"%(key), parameters, value["range"])
            else:
                code += "static int %s_hash(const char *param) {\n"%(key)
                code += "   return -1;\n"
                code += "}\n"

        code += "int param_index(ANARIDataType type, const char *subtype, const char *param) {\n"
        code += "   switch(type) {\n"
        for type_enum in self.named_types:
            code += "      case %s:\n"%(type_enum)
            code += "         return %s_index(subtype, param);\n"%type_enum

        for type_enum, value in self.anon_objects.items():
            if value:
                code += "      case %s:\n"%(type_enum)
                code += "         return %s_hash(param);\n"%type_enum
        code += "      default:\n"
        code += "         return -1;\n"
        code += "   }\n"
        code += "}\n"
        return code


parser = argparse.ArgumentParser(description="Generate debug objects for an ANARI device.")
parser.add_argument("-d", "--device", dest="devicespec", type=open, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", type=pathlib.Path, action="append", help="Path to the core and extension json root.")
parser.add_argument("-p", "--prefix", dest="prefix", help="Prefix for the classes and filenames.")
parser.add_argument("-n", "--namespace", dest="namespace", help="Namespace for the classes and filenames.")
args = parser.parse_args()

#flattened list of all input jsons in supplied directories
jsons = [entry for j in args.json for entry in j.glob("**/*.json")]

#load the device root
device = json.load(args.devicespec)
print("opened " + device["info"]["type"] + " " + device["info"]["name"])

#merge all dependencies
for x in device["info"]["dependencies"]:
    matches = [p for p in jsons if p.stem == x]
    for m in matches:
        merge_anari.merge(device, json.load(open(m)))

#generate files
gen = QueryGenerator(device)

with open(args.prefix + "Queries.cpp", mode='w') as f:
    f.write("// Copyright 2021 The Khronos Group\n")
    f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")

    f.write("#include <anari/anari.h>\n")
    f.write("namespace anari {\n")
    f.write("namespace "+args.namespace+" {\n")
    f.write(gen.preamble())
    f.write(gen.generate_subtype_query())
    f.write(gen.generate_parameter_query())
    f.write("}\n")
    f.write("}\n")
