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
        self.param_strings = set()
        self.objects = dict()
        type_enums = next(x for x in anari["enums"] if x["name"] == "ANARIDataType")
        self.type_enum_dict = {e["name"]: e for e in type_enums["values"]}

        offset = 0
        for obj in anari["objects"]:
            self.param_strings.update([p["name"] for p in obj["parameters"]])

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

            offset += length

        self.param_strings = sorted(list(self.param_strings))
        self.named_types = sorted(list(set([k[0] for k in self.named_objects])))
        self.subtype_list = sorted(list(set([k[1] for k in self.named_objects])))

    def preamble(self):
        code = "static " + hash_gen.gen_hash_function("subtype_hash", self.subtype_list)
        code += "static " + hash_gen.gen_hash_function("param_hash", self.param_strings)
        return code;

    def generate_extension_query(self):
        code = "const char ** query_extensions() {\n"
        code += "   static const char *features[] = {\n      "
        if self.anari["features"]:
            code += ",\n      ".join(["\"ANARI_%s\""%x for x in self.anari["features"]]) + ",\n"
        code += "      0\n   };\n"
        code += "   return features;\n"
        code += "}\n"
        return code

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

    def check_and_return(self, value_type_name, value):
        value_type = self.type_enum_dict[value_type_name]
        code = ""
        code += "         if("
        code += "paramType == " + value_type["name"] + " && "
        code += "infoType == " + value_type["name"] + ") {\n"
        if value_type["name"] == "ANARI_STRING":
            code += "            static const char *default_value = \"%s\";\n"%value
        else:
            code += "            static const " + value_type["baseType"]
            code += " default_value[%d]"%value_type["elements"] + " = "
            if isinstance(value, list):
                code += "{" + ", ".join([str(x) for x in value]) + "};\n"
            else:
                code += "{" + str(value) + "};\n"
        code += "            return default_value;\n"
        code += "         } else {\n"
        code += "            return nullptr;\n"
        code += "         }\n"
        return code

    def generate_parameter_info_query(self):
        code = ""
        # generate the per parameter query functions
        info_strings = ["required", "default", "minimum", "maximum", "description", "elementType", "values"]
        code += "static " + hash_gen.gen_hash_function("info_hash", info_strings)
        code += "static const int32_t anari_true = 1;"
        code += "static const int32_t anari_false = 0;"
        for obj in self.anari["objects"]:
            objname = obj["type"]
            if "name" in obj:
                objname += "_" + obj["name"]

            for param in obj["parameters"]:
                paramname = param["name"].replace(".", "_")
                code += "static const void * " + objname + "_" + paramname + "_info(ANARIDataType paramType, const char *infoName, ANARIDataType infoType) {\n"
                code += "   switch(info_hash(infoName)) {\n"

                # required info always exists
                code += "      case "+str(info_strings.index("required"))+": // required\n"
                code += "         if(infoType == ANARI_BOOL) {\n"
                if "required" in param["tags"]:
                    code += "            return &anari_true;\n"
                else:
                    code += "            return &anari_false;\n"
                code += "         } else {\n"
                code += "            return nullptr;\n"
                code += "         }\n"

                if "default" in param:
                    code += "      case "+str(info_strings.index("default"))+": // default\n"
                    code += self.check_and_return(param["types"][0], param["default"])

                if "minimum" in param:
                    code += "      case "+str(info_strings.index("minimum"))+": // minimum\n"
                    code += self.check_and_return(param["types"][0], param["minimum"])

                if "maximum" in param:
                    code += "      case "+str(info_strings.index("maximum"))+": // maximum\n"
                    code += self.check_and_return(param["types"][0], param["maximum"])

                if "description" in param:
                    code += "      case "+str(info_strings.index("description"))+": // description\n"
                    code += "         {\n"
                    code += "            const char *description = \"%s\";\n"%param["description"]
                    code += "            return description;\n"
                    code += "         }\n"

                if "elementType" in param:
                    code += "      case "+str(info_strings.index("elementType"))+": // elementType\n"
                    code += "         if(infoType == ANARI_DATA_TYPE) {\n"
                    code += "            static const ANARIDataType *values[] = {"
                    code += ", ".join(param["values"]) + ", ANARI_UNKNOWN};\n"
                    code += "            return values;\n"
                    code += "         } else {\n"
                    code += "            return nullptr;\n"
                    code += "         }\n"

                if "values" in param:
                    code += "      case "+str(info_strings.index("values"))+": // values\n"
                    code += "         if(paramType == ANARI_STRING) {\n"
                    code += "            static const char *values[] = {"
                    code += ", ".join(["\"%s\""%v for v in param["values"]]) + ", nullptr};\n"
                    code += "            return values;\n"
                    code += "         } else {\n"
                    code += "            return nullptr;\n"
                    code += "         }\n"

                code += "       default: return nullptr;\n"
                code += "   }\n"
                code += "}\n"


            code += "static const void * " + objname + "_param_info(const char *paramName, ANARIDataType paramType, const char *infoName, ANARIDataType infoType) {\n"
            code += "   switch(param_hash(paramName)) {\n"
            for param in obj["parameters"]:
                paramname = param["name"].replace(".", "_")
                code += "      case %i:\n"%self.param_strings.index(param["name"])
                code += "         return " + objname + "_" + paramname + "_info(paramType, infoName, infoType);\n"
            code += "      default:\n"
            code += "         return nullptr;\n"
            code += "   }\n"
            code += "}\n"




        for type_enum in self.named_types:
            subtypes = [key[1] for key,params in self.named_objects.items() if key[0] == type_enum]
            code += "static const void * "+type_enum+"_param_info(const char *subtype, const char *paramName, ANARIDataType paramType, const char *infoName, ANARIDataType infoType) {\n"
            code += "   switch(subtype_hash(subtype)) {\n"
            for subtype in subtypes:
                code += "      case %d:\n"%(self.subtype_list.index(subtype))
                code += "         return %s_%s_param_info(paramName, paramType, infoName, infoType);\n"%(type_enum, subtype)
            code += "      default:\n"
            code += "         return nullptr;\n"
            code += "   }\n"
            code += "}\n"

        code += "const void * query_param_info(ANARIDataType type, const char *subtype, const char *paramName, ANARIDataType paramType, const char *infoName, ANARIDataType infoType) {\n"
        code += "   switch(type) {\n"
        for type_enum in self.named_types:
            code += "      case %s:\n"%(type_enum)
            code += "         return %s_param_info(subtype, paramName, paramType, infoName, infoType);\n"%type_enum

        for type_enum in self.anon_objects.keys():
            code += "      case %s:\n"%(type_enum)
            code += "         return %s_param_info(paramName, paramType, infoName, infoType);\n"%type_enum

        code += "      default:\n"
        code += "         return nullptr;\n"
        code += "   }\n"
        code += "}\n"
        return code


        return code

parser = argparse.ArgumentParser(description="Generate query functions for an ANARI device.")
parser.add_argument("-d", "--device", dest="devicespec", type=open, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", type=pathlib.Path, action="append", help="Path to the core and extension json root.")
parser.add_argument("-p", "--prefix", dest="prefix", help="Prefix for the classes and filenames.")
parser.add_argument("-n", "--namespace", dest="namespace", help="Namespace for the classes and filenames.")
parser.add_argument("-o", "--output", dest="outdir", type=pathlib.Path, default=pathlib.Path("."), help="Output directory")
args = parser.parse_args()


#flattened list of all input jsons in supplied directories
jsons = [entry for j in args.json for entry in j.glob("**/*.json")]

#load the device root
device = json.load(args.devicespec)
print("opened " + device["info"]["type"] + " " + device["info"]["name"])

dependencies = merge_anari.crawl_dependencies(device, jsons)
#merge all dependencies
for x in dependencies:
    matches = [p for p in jsons if p.stem == x]
    for m in matches:
        merge_anari.merge(device, json.load(open(m)))

#generate files
gen = QueryGenerator(device)


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

with open(args.outdir/(args.prefix + "Queries.cpp"), mode='w') as f:
    f.write("// Copyright 2021 The Khronos Group\n")
    f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")

    f.write("#include <anari/anari.h>\n")
    f.write(begin_namespaces(args))
    f.write(gen.preamble())
    f.write(gen.generate_extension_query())
    f.write(gen.generate_subtype_query())
    f.write(gen.generate_parameter_query())
    f.write(gen.generate_parameter_info_query())
    f.write(end_namespaces(args))
