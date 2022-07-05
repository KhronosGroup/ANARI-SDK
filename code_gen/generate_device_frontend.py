# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import sys
import os
import json
import hash_gen
import merge_anari
import argparse
import pathlib
import subprocess

extra_params = {
    "ANARI_ARRAY1D" : [("const void*","appMemory"), ("ANARIMemoryDeleter","deleter"), ("const void*","userdata"), ("ANARIDataType","type"), ("uint64_t", "numItems1"), ("uint64_t", "byteStride1")],
    "ANARI_ARRAY2D" : [("const void*","appMemory"), ("ANARIMemoryDeleter","deleter"), ("const void*","userdata"), ("ANARIDataType","type"), ("uint64_t", "numItems1"), ("uint64_t", "numItems2"), ("uint64_t", "byteStride1"), ("uint64_t", "byteStride2")],
    "ANARI_ARRAY3D" : [("const void*","appMemory"), ("ANARIMemoryDeleter","deleter"), ("const void*","userdata"), ("ANARIDataType","type"), ("uint64_t", "numItems1"), ("uint64_t", "numItems2"), ("uint64_t", "numItems3"), ("uint64_t", "byteStride1"), ("uint64_t", "byteStride2"), ("uint64_t", "byteStride3")]
}

class FrontendGenerator:
    def __init__(self, anari, args):
        self.anari = anari
        self.args = args

        self.attribute_strings = {x["name"] for x in anari["attributes"]}
        self.enum_strings = set()
        self.param_strings = set()
        self.objects = dict()
        for obj in anari["objects"]:
            if not obj["type"] in self.objects:
                self.objects[obj["type"]] = list()
            if "name" in obj:
                self.objects[obj["type"]].append(obj["name"])


            self.param_strings.update([p["name"] for p in obj["parameters"]])
            for param in obj["parameters"]:
                if "ANARI_STRING" in param["types"] and "values" in param:
                    self.enum_strings.update(param["values"])

        self.all_strings = sorted(list(self.attribute_strings.union(self.enum_strings)))
        self.param_strings = sorted(list(self.param_strings))

        type_enums = next(x for x in anari["enums"] if x["name"] == "ANARIDataType")
        self.type_enum_dict = {e["name"]: e for e in type_enums["values"]}

    def hash_function(self):
        return hash_gen.gen_hash_function("parameter_string_hash", self.all_strings)

    def string_list(self):
        return "const char *param_strings[] = {\n   \"" + "\",\n   \"".join(self.all_strings) + "\"\n};\n"

    def define_list(self):
        code = "#define STRING_ENUM_unknown -1\n"
        code += "\n".join(["#define STRING_ENUM_%s %d"%(x, i) for i,x in enumerate(self.all_strings)])
        code += "\n"
        return code

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

    def declare_objects(self):
        code = ""
        code += "class ParameterPack {\n"
        code += "public:\n"
        code += "   virtual bool set(const char *paramname, ANARIDataType type, const void *mem) = 0;\n"
        code += "   virtual void unset(const char *paramname) = 0;\n"
        code += "   virtual ParameterBase& operator[](size_t idx) = 0;\n"
        code += "   virtual ParameterBase& operator[](const char *paramname) = 0;\n"
        code += "   virtual const char** paramNames() const = 0;\n"
        code += "   virtual size_t paramCount() const = 0;\n"
        code += "};\n"
        for idx, obj in enumerate(self.anari["objects"]):
            classname = obj["type"][6:].title()
            if "name" in obj:
                classname += obj["name"][0].upper() + obj["name"][1:]

            code += "class " + classname + " : public ParameterPack {\n"
            code += "public:\n"

            code += "   ANARIDevice device;\n"
            code += "   ANARIObject object;\n"

            code += "   static const int type = " + obj["type"] + ";\n"

            if "name" in obj:
                code += "   static constexpr const char *subtype = \"" + obj["name"] + "\";\n"
            else:
                code += "   static constexpr const char *subtype = nullptr;\n"

            code += "   static const uint32_t id = %d;\n"%idx

            for param in obj["parameters"]:
                code += "   Parameter<" + ", ".join(param["types"]) + "> " + param["name"].replace(".", "_") + ";\n"
            code += "\n"

            code += "   " + classname + "(ANARIDevice d, ANARIObject o);\n"

            code += "   bool set(const char *paramname, ANARIDataType type, const void *mem) override;\n"
            code += "   void unset(const char *paramname) override;\n"
            code += "   ParameterBase& operator[](size_t idx) override;\n"
            code += "   ParameterBase& operator[](const char *paramname) override;\n"
            code += "   const char** paramNames() const override;\n"
            code += "   size_t paramCount() const override;\n"

            code += "};\n"
        return code


    def set_default(self, param, indentation):
        code = ""
        paramname = param["name"].replace('.', '_')
        basetype = self.type_enum_dict[param["types"][0]]["baseType"]
        anari_type = param["types"][0]
        if isinstance(param["default"], list):
            code += indentation + basetype + " value[] = {" + ", ".join([self.format_as(x, anari_type) for x in param["default"]]) + "};\n"
        elif isinstance(param["default"], str):
            code += indentation + "const char *value = \"" + param["default"] + "\";\n"
        else:
            code += indentation + basetype + " value[] = {" + self.format_as(param["default"], anari_type) + "};\n"
        code += indentation + "%s.set(device, object, %s, value);\n"%(paramname, param["types"][0])
        return code

    def implement_objects(self):
        code = ""
        code += "static " + hash_gen.gen_hash_function("param_hash", self.param_strings, indent = "")
        for obj in self.anari["objects"]:
            classname = obj["type"][6:].title()
            if "name" in obj:
                classname += obj["name"][0].upper() + obj["name"][1:]

            code += classname + "::" + classname + "(ANARIDevice device, ANARIObject o) : device(device), object(o)"
            code += " {\n"

            for param in obj["parameters"]:
                if "default" in param:
                    code += "   {\n"
                    code += self.set_default(param, indentation="      ")
                    code += "   }\n"

            code += "}\n"
            code += "bool " + classname + "::set(const char *paramname, ANARIDataType type, const void *mem) {\n"
            code += "   int idx = param_hash(paramname);\n"
            code += "   switch(idx) {\n"
            for j,param in enumerate(obj["parameters"]):
                i = self.param_strings.index(param["name"])
                paramname = param["name"].replace('.', '_')
                code += "      case %d: //%s\n"%(i,param["name"])
                code += "         return %s.set(device, object, type, mem);\n"%paramname
            code += "      default: // unknown param\n"
            code += "         //unknown parameter\n"
            code += "         return false;\n"
            code += "   }\n"
            code += "}\n"
            code += "void " + classname + "::unset(const char *paramname) {\n"
            code += "   int idx = param_hash(paramname);\n"
            code += "   switch(idx) {\n"
            for j,param in enumerate(obj["parameters"]):
                i = self.param_strings.index(param["name"])
                paramname = param["name"].replace('.', '_')
                code += "      case %d: //%s\n"%(i,param["name"])
                if "default" in param:
                    code += "         {\n"
                    code += self.set_default(param, indentation="            ")
                    code += "         }\n"
                else:
                    code += "         %s.unset(device, object);\n"%paramname
                code += "         return;\n"
            code += "      default: // unknown param\n"
            code += "         //unknown parameter\n"
            code += "         return;\n"
            code += "   }\n"
            code += "}\n"


            code += "ParameterBase& " + classname + "::operator[](size_t idx) {\n"
            code += "   static EmptyParameter empty;\n"
            code += "   switch(idx) {\n"
            for idx, param in enumerate(obj["parameters"]):
                code += "      case %d: return "%idx + param["name"].replace(".", "_") + ";\n"
            code += "      default: return empty;\n"
            code += "   }\n"
            code += "}\n"

            code += "ParameterBase& " + classname + "::operator[](const char *paramname) {\n"
            code += "   static EmptyParameter empty;\n"
            code += "   int idx = param_hash(paramname);\n"
            code += "   switch(idx) {\n"
            for param in obj["parameters"]:
                idx = self.param_strings.index(param["name"])
                code += "      case %d: return "%idx + param["name"].replace(".", "_") + ";\n"
            code += "      default: return empty;\n"
            code += "   }\n"
            code += "}\n"

            code += "const char ** " + classname + "::paramNames() const {\n"

            code += "   static const char *paramnames[] = {\n"
            for param in obj["parameters"]:
                code += "      \"" + param["name"] + "\",\n"
            code += "      nullptr\n"
            code += "   };\n"
            code += "   return paramnames;\n"
            code += "}\n"

            code += "size_t " + classname + "::paramCount() const {\n"
            code += "   return %d;\n"%len(obj["parameters"])
            code += "}\n"

            code += "\n"
        return code

    def implement_device(self):
        code = ""
        objnames = set()
        for key, names in self.objects.items():
            objnames.update(names)
        objnames = sorted(list(objnames))

        code += "static " + hash_gen.gen_hash_function("obj_hash", objnames, indent = "")

        devicename = self.args.prefix + "Device"
        for key, names in self.objects.items():
            if key == "ANARI_DEVICE":
                continue
            factoryname = "".join(key.title().split("_")[1:])

            if names:
                code += "ANARI" + factoryname + " "
                code += devicename + "::new" + factoryname + "(const char *type) {\n"
                code += "   int idx = obj_hash(type);\n"
                code += "   switch(idx) {\n"
                for name in names:
                    classname = key[6:].title() + name[0].upper() + name[1:]

                    code += "      case %d: //%s\n"%(objnames.index(name), name)
                    code += "         return allocate<ANARI"+factoryname+", "+classname+">();\n"

                code += "      default: // unknown object\n"
                code += "         return 0;\n"
                code += "   }\n"
                code += "   return 0;\n"
                code += "}\n"
            else:
                code += "ANARI" + factoryname + " "
                code += devicename + "::new" + factoryname + "("
                if key in extra_params:
                    code += ", ".join("%s %s"%x for x in extra_params[key])
                code += ") {\n"
                classname = key[6:].title()
                code += "   return allocate<ANARI"+factoryname+", "+classname+">("
                if key in extra_params:
                    code += ", ".join(x[1] for x in extra_params[key])
                code += ");\n"
                code += "}\n"
        return code

parser = argparse.ArgumentParser(description="Generate debug objects for an ANARI device.")
parser.add_argument("-d", "--device", dest="devicespec", type=pathlib.Path, required=True, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", required=True, type=pathlib.Path, action="append", help="Path to the core and extension json root.")
parser.add_argument("-p", "--prefix", dest="prefix", required=True, help="Prefix for the classes and filenames.")
parser.add_argument("--namespace", dest="namespace", required=True, help="Namespace for generated code. Also used as library name.")
parser.add_argument("--name", dest="name", required=True, help="Name of the library and device.")
parser.add_argument("-f", "--force", dest="force", action="store_true", help="Force regeneration of all files. This will overwrite edited files.")
parser.add_argument("-l", "--header", dest="header", help="File containing a header to be inserted at the top of source files")
parser.add_argument("-o", "--output", dest="outdir", type=pathlib.Path, default=pathlib.Path("."), help="Output directory")
parser.add_argument("--omit_target_prefix", dest="omitprefix", action="store_true", help=argparse.SUPPRESS)
args = parser.parse_args()

try:
    devicespec = args.devicespec.relative_to(args.outdir)
except ValueError as err:
    print("device specification needs to be in output directory.")
    print(err)
    sys.exit(1)

templatepath = pathlib.Path(__file__).parent / "templates"

#flattened list of all input jsons in supplied directories
jsons = [entry for j in args.json for entry in j.glob("**/*.json")]

#load the device root
device = json.load(open(args.devicespec))
print("opened " + device["info"]["type"] + " " + device["info"]["name"])

#load header if present
header = ""
if args.header:
    header = open(args.header).read()

dependencies = merge_anari.crawl_dependencies(device, jsons)
#merge all dependencies
for x in dependencies:
    matches = [p for p in jsons if p.stem == x]
    for m in matches:
        merge_anari.merge(device, json.load(open(m)))

#generate files
gen = FrontendGenerator(device, args)


rootdir = args.outdir
gendir = args.outdir/"generated"
srcdir = args.outdir/"src"

rootdir.mkdir(parents=True, exist_ok=True)
gendir.mkdir(parents=True, exist_ok=True)
srcdir.mkdir(parents=True, exist_ok=True)

targetprefix = "anari::"
if args.omitprefix:
    targetprefix = ""

namespaces = args.namespace.split("::")
begin_namespaces = "".join(["namespace %s{\n"%n for n in namespaces])
end_namespaces = "".join(["} //namespace %s\n"%n for n in namespaces])



with open(gendir/(args.prefix + "String.h"), mode='w') as f:
    f.write(header)
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")
    f.write("#pragma once\n")
    f.write(begin_namespaces)
    f.write(gen.define_list())
    f.write("extern const char *param_strings[];\n")
    f.write("int parameter_string_hash(const char *str);\n")
    f.write(end_namespaces)

with open(gendir/(args.prefix + "String.cpp"), mode='w') as f:
    f.write(header)
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")
    f.write("#include \""+args.prefix+"String.h\"\n")
    f.write("#include <cstdint>\n")
    f.write(begin_namespaces)
    f.write(gen.hash_function())
    f.write(gen.string_list())
    f.write(end_namespaces)

with open(gendir/(args.prefix + "Objects.h"), mode='w') as f:
    f.write(header)
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")
    f.write("#pragma once\n")
    f.write("#include \""+args.prefix+"Parameter.h\"\n")
    f.write(begin_namespaces)
    f.write(gen.declare_objects())
    f.write(end_namespaces)

with open(gendir/(args.prefix + "Objects.cpp"), mode='w') as f:
    f.write(header)
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")

    f.write("#include <stdint.h>\n")
    f.write("#include \""+args.prefix+"Objects.h\"\n")
    f.write(begin_namespaces)
    f.write(gen.implement_objects())
    f.write(end_namespaces)

with open(gendir/(args.prefix + "DeviceFactories.cpp"), mode='w') as f:
    f.write(header)
    f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
    f.write("// Don't make changes to this directly\n\n")

    f.write("#include \""+args.prefix+"Device.h\"\n")
    f.write("#include \""+args.prefix+"Objects.h\"\n")
    f.write("#include \""+args.prefix+"Specializations.h\"\n")
    f.write(begin_namespaces)
    f.write(gen.implement_device())
    f.write(end_namespaces)

def generate_if_not_present(targetdir, name, header, noprefix=False):
    filename = args.prefix + name
    if noprefix:
        filename = name
    filename = targetdir/filename
    if args.force and filename.is_file():
        os.rename(filename, filename.parent/(filename.name + ".old"))
    if not filename.is_file():
        with open(filename, mode='w') as f:
            infile = (templatepath/name).open().read()
            infile = infile.replace("$libraryname", args.name)
            infile = infile.replace("$namespace", args.namespace)
            infile = infile.replace("$begin_namespaces", begin_namespaces)
            infile = infile.replace("$end_namespaces", end_namespaces)
            infile = infile.replace("$prefix", args.prefix)
            infile = infile.replace("$generator", os.path.basename(__file__))
            infile = infile.replace("$device_json", str(args.devicespec.relative_to(rootdir)))
            infile = infile.replace("$device_json", "foo")
            infile = infile.replace("$generated_path", str(gendir.relative_to(rootdir)))
            infile = infile.replace("$source_path", str(srcdir.relative_to(rootdir)))
            infile = infile.replace("$target_prefix", targetprefix)
            infile = infile.replace("$template", name)
            f.write(header)
            f.write(infile)

generate_if_not_present(gendir, "Parameter.h", header)
generate_if_not_present(srcdir, "Object.h", header)
generate_if_not_present(srcdir, "ArrayObjects.h", header)
generate_if_not_present(srcdir, "ArrayObjects.cpp", header)
generate_if_not_present(srcdir, "FrameObject.h", header)
generate_if_not_present(srcdir, "FrameObject.cpp", header)
generate_if_not_present(srcdir, "Device.h", header)
generate_if_not_present(srcdir, "Device.cpp", header)
generate_if_not_present(srcdir, "Specializations.h", header)
generate_if_not_present(rootdir, "CMakeLists.txt", header.replace("//","#"), noprefix = True)


forwarded = ["--device", args.devicespec]
forwarded += ["--prefix", args.prefix]
forwarded += ["--namespace", args.namespace]
for x in args.json:
    forwarded += ["--json", str(x)]

scriptdir = pathlib.Path(__file__).parent
subprocess.run([sys.executable, scriptdir/"generate_queries.py","--output", str(gendir)]+forwarded)
subprocess.run([sys.executable, scriptdir/"generate_debug_objects.py","--output", str(gendir)]+forwarded)