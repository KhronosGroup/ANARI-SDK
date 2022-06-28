# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import sys
import os
import json
import hash_gen
import merge_anari
import argparse
import pathlib

class DebugGenerator:
    def __init__(self, anari):
        self.anari = anari
        self.named_objects = dict()
        self.anon_objects = dict()
        self.parameter_dict = dict()
        self.parameter_index = list()
        self.objects = dict()
        type_enums = next(x for x in anari["enums"] if x["name"] == "ANARIDataType")
        self.type_enum_dict = {e["name"]: e for e in type_enums["values"]}

        features = set()

        offset = 0
        for obj in anari["objects"]:
            if not obj["type"] in self.objects:
                self.objects[obj["type"]] = []

            if "feature" in obj:
                features.add(obj["feature"])

            parameter_list = []
            parameter_type_list = []
            for param in obj["parameters"]:
                parameter_list.append(param)
                if "feature" in param:
                    features.add(param["feature"])

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
        self.feature_list = sorted(list(features))


    def generate_validation_objects_decl(self, factoryname):
        code = ""
        code += "class " + factoryname + " : public anari::debug_device::ObjectFactory {\n"
        code += "public:\n"
        code += "   anari::debug_device::DebugObjectBase* new_volume(const char *name, anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_geometry(const char *name, anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_spatial_field(const char *name, anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_light(const char *name, anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_camera(const char *name, anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_material(const char *name, anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_sampler(const char *name, anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_renderer(const char *name, anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_device(anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_array1d(anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_array2d(anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_array3d(anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_frame(anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_group(anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_instance(anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_world(anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   anari::debug_device::DebugObjectBase* new_surface(anari::debug_device::DebugDevice *td, ANARIObject wh, ANARIObject h) override;\n"
        code += "   void print_summary(anari::debug_device::DebugDevice *td) override;\n"
        code += "   void use_feature(int feature);\n"
        code += "};\n"
        return code

    def generate_validation_objects_impl(self, factoryname):
        base = "DebugObject"
        code = ""
        code += "namespace {\n"
        for obj in self.anari["objects"]:
            params = obj["parameters"]
            objname = obj["type"][6:].lower()
            subtype = ""
            if "name" in obj:
                objname += "_" + obj["name"]
                subtype = obj["name"]
            code += "class " + objname + " : public " + base + "<" + obj["type"] + "> {\n"
            if params:
                code += "   static " + hash_gen.gen_hash_function("param_hash", [p["name"] for p in params], indent = "   ")
                code += "   public:\n"
                code += "   " + objname + "(DebugDevice *td, " + factoryname + " *factory, ANARIObject wh, ANARIObject h)"
                code += ": " + base + "(td, wh, h) { (void)factory; }\n"
                code += "   void setParameter(const char *paramname, ANARIDataType paramtype, const void *mem) {\n"
                code += "      " + base + "::setParameter(paramname, paramtype, mem);\n"
                code += "      int idx = param_hash(paramname);\n"
                code += "      switch(idx) {\n"
                for i in range(0, len(params)):
                    paramname = params[i]["name"].replace('.', '_')
                    code += "         case %d: { //%s\n"%(i,params[i]["name"])
                    code += "            ANARIDataType %s_types[] = {"%paramname
                    code += ",".join(params[i]["types"])
                    code += ", ANARI_UNKNOWN};\n";
                    code += "            check_type(%s, \"%s\", paramname, paramtype, %s_types);\n"%(obj["type"], subtype, paramname)
                    code += "            return;\n"
                    code += "         }\n"
                code += "         default: // unknown param\n"
                code += "            unknown_parameter(%s, \"%s\", paramname, paramtype);\n"%(obj["type"], subtype)
                code += "            return;\n"
                code += "      }\n"
                code += "   }\n"

            code += "   void commit() {\n"
            code += "      " + base + "::commit();\n"
            code += "   }\n"
            code += "   const char* getSubtype() {\n"
            code += "      return \"%s\";\n"%subtype
            code += "   }\n"
            code += "};\n"
        code += "}\n"

        named_type_set = sorted(list({x[0] for x in self.named_objects.keys()}))

        for t in named_type_set:
            type_name = t[6:].lower()
            subtypes = sorted([x[1] for x in self.named_objects.keys() if x[0] == t])
            code += "static " + hash_gen.gen_hash_function(type_name+"_object_hash", subtypes)
            code += "DebugObjectBase* " + factoryname + "::new_" + type_name + "(const char *name, DebugDevice *td, ANARIObject wh, ANARIObject h) {\n"
            code += "   int idx = " + type_name + "_object_hash(name);\n"
            code += "   switch(idx) {\n"
            for i in range(0, len(subtypes)):
                code += "      case %d:\n"%i
                code += "         return new " + t[6:].lower() + "_" + subtypes[i] + "(td, this, wh, h);\n"
            code += "      default:\n"
            code += "         unknown_subtype(td, " + t + ", name);\n"
            code += "         return new SubtypedDebugObject<"+t+">(td, wh, h, name);\n"
            code += "   }\n"
            code += "}\n"

        for t in sorted(self.anon_objects.keys()):
            type_name = t[6:].lower()
            code += "DebugObjectBase* " + factoryname + "::new_" + type_name + "(DebugDevice *td, ANARIObject wh, ANARIObject h) {\n"
            code += "   return new " + type_name + "(td, this, wh, h);\n"
            code += "}\n"

        code += "void " + factoryname + "::print_summary(DebugDevice *td) {\n"
        code += "   (void)td;\n"
        code += "}\n"
        return code


parser = argparse.ArgumentParser(description="Generate debug objects for an ANARI device.")
parser.add_argument("-d", "--device", dest="devicespec", type=open, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", type=pathlib.Path, action="append", help="Path to the core and extension json root.")
parser.add_argument("-p", "--prefix", dest="prefix", help="Prefix for the classes and filenames.")
parser.add_argument("--header", dest="header", action="store_true", default=False, help="Generate a separate header.")
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
        feature = json.load(open(m))
        merge_anari.tag_feature(feature)
        merge_anari.merge(device, feature)

#generate files
gen = DebugGenerator(device)

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

if args.header:
    with open(args.outdir/(args.prefix + "DebugFactory.h"), mode='w') as f:
        f.write("// Copyright 2021 The Khronos Group\n")
        f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
        f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
        f.write("// Don't make changes to this directly\n\n")

        f.write("#pragma once\n")
        f.write("#include \"anari/ext/debug/DebugObject.h\"\n")
        f.write(begin_namespaces(args))
        f.write(gen.generate_validation_objects_decl(args.prefix + "DebugFactory"))
        f.write(end_namespaces(args))

with open(args.outdir/(args.prefix + "DebugFactory.cpp"), mode='w') as f:
    f.write("// Copyright 2021 The Khronos Group\n")
    f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")


    if args.header:
        f.write("#include \""+args.prefix+"DebugFactory.h\"\n")
        f.write("using namespace anari::debug_device;\n")
        f.write(begin_namespaces(args))
    else:
        f.write("#include \"anari/ext/debug/DebugObject.h\"\n")
        f.write("using namespace anari::debug_device;\n")
        f.write(begin_namespaces(args))
        f.write(gen.generate_validation_objects_decl(args.prefix + "DebugFactory"))


    f.write(gen.generate_validation_objects_impl(args.prefix + "DebugFactory"))

    f.write("anari::debug_device::ObjectFactory* getDebugFactory() {\n")
    f.write("   static " + args.prefix + "DebugFactory f;\n")
    f.write("   return &f;\n")
    f.write("}\n")
    f.write(end_namespaces(args))
