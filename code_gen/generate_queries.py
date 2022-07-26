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
        self.attribute_list = [x["name"] for x in anari["attributes"]]
        self.info_strings = ["required", "default", "minimum", "maximum", "description", "elementType", "value", "sourceFeature", "feature", "parameter"]

    def format_as(self, value, anari_type):
        basetype = self.type_enum_dict[anari_type]["baseType"]
        if basetype == "float":
            return "%ff"%value
        elif basetype == "double":
            return "%f"%value
        elif basetype.startswith("uint") or basetype.startswith("int"):
            macro = basetype.upper()[:-1]+'C'
            return "%s(%d)"%(macro,value)
        else:
            return str(x)

    def preamble(self):
        code = "static " + hash_gen.gen_hash_function("subtype_hash", self.subtype_list)
        code += "static " + hash_gen.gen_hash_function("param_hash", self.param_strings)
        code += "static " + hash_gen.gen_hash_function("info_hash", self.info_strings)
        code += "static const int32_t anari_true = 1;"
        code += "static const int32_t anari_false = 0;"
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
                code += "{" + ", ".join([self.format_as(x, value_type_name) for x in value]) + "};\n"
            else:
                code += "{" + self.format_as(value, value_type_name) + "};\n"
        code += "            return default_value;\n"
        code += "         } else {\n"
        code += "            return nullptr;\n"
        code += "         }\n"
        return code

    def generate_parameter_info_query(self):
        code = ""
        # generate the per parameter query functions

        for obj in self.anari["objects"]:
            objname = obj["type"]
            if "name" in obj:
                objname += "_" + obj["name"]

            for param in obj["parameters"]:
                paramname = param["name"].replace(".", "_")
                code += "static const void * " + objname + "_" + paramname + "_info(ANARIDataType paramType, int infoName, ANARIDataType infoType) {\n"
                code += "   (void)paramType;\n"
                code += "   switch(infoName) {\n"

                # required info always exists
                code += "      case "+str(self.info_strings.index("required"))+": // required\n"
                code += "         if(infoType == ANARI_BOOL) {\n"
                if "required" in param["tags"]:
                    code += "            return &anari_true;\n"
                else:
                    code += "            return &anari_false;\n"
                code += "         } else {\n"
                code += "            return nullptr;\n"
                code += "         }\n"

                if "default" in param:
                    code += "      case "+str(self.info_strings.index("default"))+": // default\n"
                    code += self.check_and_return(param["types"][0], param["default"])

                if "minimum" in param:
                    code += "      case "+str(self.info_strings.index("minimum"))+": // minimum\n"
                    code += self.check_and_return(param["types"][0], param["minimum"])

                if "maximum" in param:
                    code += "      case "+str(self.info_strings.index("maximum"))+": // maximum\n"
                    code += self.check_and_return(param["types"][0], param["maximum"])

                if "description" in param:
                    code += "      case "+str(self.info_strings.index("description"))+": // description\n"
                    code += "         {\n"
                    code += "            static const char *description = \"%s\";\n"%param["description"]
                    code += "            return description;\n"
                    code += "         }\n"

                if "elementType" in param:
                    code += "      case "+str(self.info_strings.index("elementType"))+": // elementType\n"
                    code += "         if(infoType == ANARI_TYPE_LIST) {\n"
                    code += "            static const ANARIDataType values[] = {"
                    code += ", ".join(param["elementType"]) + ", ANARI_UNKNOWN};\n"
                    code += "            return values;\n"
                    code += "         } else {\n"
                    code += "            return nullptr;\n"
                    code += "         }\n"

                if "values" in param:
                    code += "      case "+str(self.info_strings.index("value"))+": // value\n"
                    code += "         if(paramType == ANARI_STRING && infoType == ANARI_STRING_LIST) {\n"
                    code += "            static const char *values[] = {"
                    code += ", ".join(["\"%s\""%v for v in param["values"]]) + ", nullptr};\n"
                    code += "            return values;\n"
                    code += "         } else {\n"
                    code += "            return nullptr;\n"
                    code += "         }\n"

                if "attribute" in param["tags"]:
                    code += "      case "+str(self.info_strings.index("value"))+": // value\n"
                    code += "         if(paramType == ANARI_STRING && infoType == ANARI_STRING_LIST) {\n"
                    code += "            static const char *values[] = {"
                    code += ", ".join(["\"%s\""%v for v in self.attribute_list]) + ", nullptr};\n"
                    code += "            return values;\n"
                    code += "         } else {\n"
                    code += "            return nullptr;\n"
                    code += "         }\n"

                if "sourceFeature" in param:
                    code += "      case "+str(self.info_strings.index("sourceFeature"))+": // sourceFeature\n"
                    code += "         if(infoType == ANARI_STRING) {\n"
                    code += "            static const char *feature = \"%s\";\n"%param["sourceFeature"]
                    code += "            return feature;\n"
                    code += "         } else if(infoType == ANARI_INT32) {\n"
                    code += "            static const int32_t value = %d;\n"%self.anari["features"].index(param["sourceFeature"])
                    code += "            return &value;\n"
                    code += "         }\n"

                code += "      default: return nullptr;\n"
                code += "   }\n"
                code += "}\n"


            code += "static const void * " + objname + "_param_info(const char *paramName, ANARIDataType paramType, int infoName, ANARIDataType infoType) {\n"
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
            code += "static const void * "+type_enum+"_param_info(const char *subtype, const char *paramName, ANARIDataType paramType, int infoName, ANARIDataType infoType) {\n"
            code += "   switch(subtype_hash(subtype)) {\n"
            for subtype in subtypes:
                code += "      case %d:\n"%(self.subtype_list.index(subtype))
                code += "         return %s_%s_param_info(paramName, paramType, infoName, infoType);\n"%(type_enum, subtype)
            code += "      default:\n"
            code += "         return nullptr;\n"
            code += "   }\n"
            code += "}\n"

        code += "const void * query_param_info_enum(ANARIDataType type, const char *subtype, const char *paramName, ANARIDataType paramType, int infoName, ANARIDataType infoType) {\n"
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

        code += "const void * query_param_info(ANARIDataType type, const char *subtype, const char *paramName, ANARIDataType paramType, const char *infoNameString, ANARIDataType infoType) {\n"
        code += "   int infoName = info_hash(infoNameString);"
        code += "   return query_param_info_enum(type, subtype, paramName, paramType, infoName, infoType);"
        code += "}"

        return code

    def generate_object_info_query(self):
        code = ""
        for obj in self.anari["objects"]:
            objname = obj["type"]
            if "name" in obj:
                objname += "_" + obj["name"]

            code += "static const void * " + objname + "_info(int infoName, ANARIDataType infoType) {\n"
            code += "   switch(infoName) {\n"
            if "description" in obj:
                code += "      case "+str(self.info_strings.index("description"))+": // description\n"
                code += "         {\n"
                code += "            static const char *description = \"%s\";\n"%obj["description"]
                code += "            return description;\n"
                code += "         }\n"

            if "parameters" in obj:
                code += "      case "+str(self.info_strings.index("parameter"))+": // parameter\n"
                code += "         if(infoType == ANARI_PARAMETER_LIST) {\n"
                code += "            static const ANARIParameter parameters[] = {\n"
                for param in obj["parameters"]:
                    for t in param["types"]:
                        code += "               {\"%s\", %s},\n"%(param["name"], t)
                code += "               {0, ANARI_UNKNOWN}\n"
                code += "            };\n"
                code += "            return parameters;\n"
                code += "         } else {\n"
                code += "            return nullptr;\n"
                code += "         }\n"

            if "sourceFeature" in obj:
                code += "      case "+str(self.info_strings.index("sourceFeature"))+": // sourceFeature\n"
                code += "         if(infoType == ANARI_STRING) {\n"
                code += "            static const char *feature = \"%s\";\n"%obj["sourceFeature"]
                code += "            return feature;\n"
                code += "         } else if(infoType == ANARI_INT32) {\n"
                code += "            static const int value = %d;\n"%self.anari["features"].index(obj["sourceFeature"])
                code += "            return &value;\n"
                code += "         } else {\n"
                code += "            return nullptr;\n"
                code += "         }\n"

            if "feature" in obj:
                code += "      case "+str(self.info_strings.index("feature"))+": // feature\n"
                code += "         if(infoType == ANARI_STRING_LIST) {\n"
                code += "            static const char *features[] = {\n"
                code += "               " + ",\n               ".join(["\"ANARI_%s\""%f for f in obj["feature"]])+",\n"
                code += "               0\n"
                code += "            };\n"
                code += "            return features;\n"
                code += "         } else {\n"
                code += "            return nullptr;\n"
                code += "         }\n"
            elif obj["type"] == "ANARI_DEVICE" or obj["type"] == "ANARI_RENDERER":
                code += "      case "+str(self.info_strings.index("feature"))+": // feature\n"
                code += "         if(infoType == ANARI_STRING_LIST) {\n"
                code += "            static const char *features[] = {\n"
                code += "               " + ",\n               ".join(["\"ANARI_%s\""%f for f in self.anari["features"]])+",\n"
                code += "               0\n"
                code += "            };\n"
                code += "            return features;\n"
                code += "         } else {\n"
                code += "            return nullptr;\n"
                code += "         }\n"


            code += "      default: return nullptr;\n"
            code += "   }\n"
            code += "}\n"



        for type_enum in self.named_types:
            subtypes = [key[1] for key,params in self.named_objects.items() if key[0] == type_enum]
            code += "static const void * "+type_enum+"_info(const char *subtype, int infoName, ANARIDataType infoType) {\n"
            code += "   switch(subtype_hash(subtype)) {\n"
            for subtype in subtypes:
                code += "      case %d:\n"%(self.subtype_list.index(subtype))
                code += "         return %s_%s_info(infoName, infoType);\n"%(type_enum, subtype)
            code += "      default:\n"
            code += "         return nullptr;\n"
            code += "   }\n"
            code += "}\n"

        code += "const void * query_object_info_enum(ANARIDataType type, const char *subtype, int infoName, ANARIDataType infoType) {\n"
        code += "   switch(type) {\n"
        for type_enum in self.named_types:
            code += "      case %s:\n"%(type_enum)
            code += "         return %s_info(subtype, infoName, infoType);\n"%type_enum

        for type_enum in self.anon_objects.keys():
            code += "      case %s:\n"%(type_enum)
            code += "         return %s_info(infoName, infoType);\n"%type_enum

        code += "      default:\n"
        code += "         return nullptr;\n"
        code += "   }\n"
        code += "}\n"

        code += "const void * query_object_info(ANARIDataType type, const char *subtype, const char *infoNameString, ANARIDataType infoType) {\n"
        code += "   int infoName = info_hash(infoNameString);"
        code += "   return query_object_info_enum(type, subtype, infoName, infoType);"
        code += "}\n"

        return code

    def generate_query_declarations(self):
        code = ""
        for idx, info in enumerate(self.info_strings):
            code += "#define ANARI_INFO_%s %d\n"%(info, idx)
        code += "const int extension_count = %d;\n"%len(self.anari["features"])
        code += "const char ** query_extensions();\n"
        code += "const char ** query_object_types(ANARIDataType type);\n"
        code += "const ANARIParameter * query_params(ANARIDataType type, const char *subtype);\n"
        code += "const void * query_param_info_enum(ANARIDataType type, const char *subtype, const char *paramName, ANARIDataType paramType, int infoName, ANARIDataType infoType);\n"
        code += "const void * query_param_info(ANARIDataType type, const char *subtype, const char *paramName, ANARIDataType paramType, const char *infoNameString, ANARIDataType infoType);\n"
        code += "const void * query_object_info_enum(ANARIDataType type, const char *subtype, int infoName, ANARIDataType infoType);\n"
        code += "const void * query_object_info(ANARIDataType type, const char *subtype, const char *infoNameString, ANARIDataType infoType);\n"
        return code

parser = argparse.ArgumentParser(description="Generate query functions for an ANARI device.")
parser.add_argument("-d", "--device", dest="devicespec", type=open, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", type=pathlib.Path, action="append", help="Path to the core and extension json root.")
parser.add_argument("-p", "--prefix", dest="prefix", help="Prefix for the classes and filenames.")
parser.add_argument("-n", "--namespace", dest="namespace", help="Namespace for the classes and filenames.")
parser.add_argument("-o", "--output", dest="outdir", type=pathlib.Path, default=pathlib.Path("."), help="Output directory")
parser.add_argument("--includefile", dest="include", action="store_true", help="Generate a header file")
args = parser.parse_args()


#flattened list of all input jsons in supplied directories
jsons = [entry for j in args.json for entry in j.glob("**/*.json")]

#load the device root
device = json.load(args.devicespec)
merge_anari.tag_feature(device)
print("opened " + device["info"]["type"] + " " + device["info"]["name"])

dependencies = merge_anari.crawl_dependencies(device, jsons)
#merge all dependencies
for x in dependencies:
    matches = [p for p in jsons if p.stem == x]
    for m in matches:
        feature = json.load(open(m))
        merge_anari.tag_feature(feature)
        merge_anari.merge(device, feature)

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

    f.write("#include <stdint.h>\n")
    f.write("#include <anari/anari.h>\n")
    f.write(begin_namespaces(args))
    f.write(gen.preamble())
    f.write(gen.generate_extension_query())
    f.write(gen.generate_subtype_query())
    f.write(gen.generate_parameter_info_query())
    f.write(gen.generate_object_info_query())
    f.write(end_namespaces(args))

if args.include:
    with open(args.outdir/(args.prefix + "Queries.h"), mode='w') as f:
        f.write("// Copyright 2021 The Khronos Group\n")
        f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
        f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
        f.write("// Don't make changes to this directly\n\n")

        f.write("#include <anari/anari.h>\n")
        f.write(begin_namespaces(args))
        f.write(gen.generate_query_declarations())
        f.write(end_namespaces(args))