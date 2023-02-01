# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

import sys
import os
import json

import merge_anari

def write_anari_enums(filename, anari):
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    with open(filename, mode='w') as f:

        f.write("// Copyright 2021 The Khronos Group\n")
        f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
        f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
        f.write("// Don't make changes to this directly\n\n")

        f.write("""
#pragma once
#ifdef __cplusplus
struct ANARIDataType
{
  ANARIDataType() = default;
  constexpr ANARIDataType(int v) noexcept : value(v) {}
  constexpr operator int() const noexcept { return value; }
  constexpr bool operator==(int v) const noexcept { return v == value; }
 private:
  int value;
};
constexpr bool operator<(ANARIDataType v1, ANARIDataType v2)
{
  return static_cast<int>(v1) < static_cast<int>(v2);
}
#define ANARI_DATA_TYPE_DEFINE(v) ANARIDataType(v)
#else
typedef int ANARIDataType;
#define ANARI_DATA_TYPE_DEFINE(v) v
#endif
""")

        # generate enums
        for enum in anari['enums']:
            if enum['name'] != 'ANARIDataType':
                f.write('\ntypedef '+enum['baseType']+' '+enum['name']+';\n')
            for value in enum['values']:
                if enum['name'] == 'ANARIDataType':
                    f.write('#define '+value['name']+' ANARI_DATA_TYPE_DEFINE('+str(value['value'])+')\n')
                else:
                    f.write('#define '+value['name']+' '+str(value['value'])+'\n')

def write_anari_header(filename, anari):
    os.makedirs(os.path.dirname(filename), exist_ok=True)

    with open(filename, mode='w') as f:
        f.write("// Copyright 2021 The Khronos Group\n")
        f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
        f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
        f.write("// Don't make changes to this directly\n\n")

        f.write("""
#pragma once

#include <stdint.h>
#include <sys/types.h>

#ifndef NULL
#if __cplusplus >= 201103L
#define NULL nullptr
#else
#define NULL 0
#endif
#endif

#define ANARI_INVALID_HANDLE NULL

#include "anari_version.h"
#include "anari_enums.h"

#ifdef _WIN32
#ifdef ANARI_STATIC_DEFINE
#define ANARI_INTERFACE
#else
#ifdef anari_EXPORTS
#define ANARI_INTERFACE __declspec(dllexport)
#else
#define ANARI_INTERFACE __declspec(dllimport)
#endif
#endif
#elif defined __GNUC__
#define ANARI_INTERFACE __attribute__((__visibility__("default")))
#else
#define ANARI_INTERFACE
#endif

#ifdef __GNUC__
#define ANARI_DEPRECATED __attribute__((deprecated))
#elif defined(_MSC_VER)
#define ANARI_DEPRECATED __declspec(deprecated)
#else
#define ANARI_DEPRECATED
#endif

#ifdef __cplusplus
// C++ DOES support default initializers
#define ANARI_DEFAULT_VAL(a) = a
#else
/* C99 does NOT support default initializers, so we use this macro
   to define them away */
#define ANARI_DEFAULT_VAL(a)
#endif
""")

        # generate opaque types
        f.write('#ifdef __cplusplus\n')
        f.write('namespace anari {\n')
        f.write('namespace api {\n')

        for opaque in anari['opaqueTypes']:
            if 'parent' in opaque:
                f.write('struct '+opaque['name'][5:]+' : public '+opaque['parent'][5:]+' {};\n')
            else:
                f.write('struct '+opaque['name'][5:]+' {};\n')

        f.write('} // namespace api\n')
        f.write('} // namespace anari\n')

        for opaque in anari['opaqueTypes']:
            f.write('typedef anari::api::'+opaque['name'][5:]+' *'+opaque['name']+';\n')

        f.write('#else\n')

        for opaque in anari['opaqueTypes']:
            f.write('typedef void* '+opaque['name']+';\n')

        f.write('#endif\n')

        f.write("""
#ifdef __cplusplus
extern "C" {
#endif

""")

        # generate structs
        for struct in anari['structs']:
            f.write('typedef struct {\n')

            if struct['members']:
                for member in struct['members']:
                    if 'elements' in member:
                        f.write('  '+member['type']+' '+member['name']+'['+str(member['elements'])+'];\n')
                    else:
                        f.write('  '+member['type']+' '+member['name']+';\n')

            f.write('} '+struct['name']+';\n')

        f.write('\n')
        # generate function prototypes
        for fun in anari['functionTypedefs']:
            f.write('typedef '+fun['returnType']+' (*'+fun['name']+')(')

            if fun['arguments']:
                arg = fun['arguments'][0]
                f.write(arg['type']+' '+arg['name'])
                for arg in fun['arguments'][1:]:
                    if 'default' in arg:
                        f.write(', '+arg['type']+' '+arg['name']+' ANARI_DEFAULT_VAL('+arg['default']+')')
                    else:
                        f.write(', '+arg['type']+' '+arg['name'])

            f.write(');\n')

        f.write('\n')
        # generate function prototypes
        for fun in anari['functions']:
            f.write('ANARI_INTERFACE '+fun['returnType']+' '+fun['name']+'(')

            if fun['arguments']:
                arg = fun['arguments'][0]
                f.write(arg['type']+' '+arg['name'])
                for arg in fun['arguments'][1:]:
                    if 'default' in arg:
                        f.write(', '+arg['type']+' '+arg['name']+' ANARI_DEFAULT_VAL('+arg['default']+')')
                    else:
                        f.write(', '+arg['type']+' '+arg['name'])

            f.write(');\n')

        f.write("""
#ifdef __cplusplus
} // extern "C"
#endif
""")


def write_anari_type_query_helper(filename, anari):
    os.makedirs(os.path.dirname(filename), exist_ok=True)

    with open(filename, mode='w') as f:
        f.write("// Copyright 2021 The Khronos Group\n")
        f.write("// SPDX-License-Identifier: Apache-2.0\n\n")
        f.write("// This file was generated by "+os.path.basename(__file__)+"\n")
        f.write("// Don't make changes to this directly\n\n")

        f.write("""
#pragma once

#include <anari/anari.h>

#ifdef __cplusplus

#include <utility>
namespace anari {

template<int type>
struct ANARITypeProperties { };

""")

        enums = next(x for x in anari['enums'] if x['name']=='ANARIDataType')
        for enum in enums['values']:
            f.write('template<>\n')
            f.write('struct ANARITypeProperties<'+enum['name']+'> {\n')
            f.write('    using base_type = '+enum['baseType']+';\n')
            f.write('    static const int components = '+str(enum['elements'])+';\n')
            f.write('    using array_type = base_type['+str(enum['elements'])+'];\n')
            f.write('    static constexpr const char* enum_name = "'+enum['name']+'";\n')
            f.write('    static constexpr const char* type_name = "'+enum['baseType']+'";\n')
            f.write('    static constexpr const char* array_name = "'+enum['baseType']+'['+str(enum['elements'])+']";\n')
            f.write('    static constexpr const char* var_name = "var'+enum['name'][6:].lower()+'";\n')
            f.write('};\n')

        f.write("""
template <typename R, template<int> class F, typename... Args>
R anariTypeInvoke(ANARIDataType type, Args&&... args) {
    switch (type) {
""")
        for enum in enums['values']:
            f.write('        case '+str(enum['value'])+': return F<'+str(enum['value'])+'>()(std::forward<Args>(args)...);\n')
        f.write('        default: return F<ANARI_UNKNOWN>()(std::forward<Args>(args)...);\n')
        f.write('    }\n')
        f.write('}\n')

        f.write('#endif\n')



        f.write("""
inline size_t sizeOf(ANARIDataType type) {
    switch (type) {
""")
        for enum in enums['values']:
            f.write('        case '+enum['name']+': return sizeof('+enum['baseType']+')*'+str(enum['elements'])+';\n')
        f.write('        default: return 4;\n')
        f.write('    }\n')
        f.write('}\n')



        f.write("""
inline size_t componentsOf(ANARIDataType type) {
    switch (type) {
""")
        for enum in enums['values']:
            f.write('        case '+enum['name']+': return '+str(enum['elements'])+';\n')
        f.write('        default: return 1;\n')
        f.write('    }\n')
        f.write('}\n')



        f.write("""
inline const char* toString(ANARIDataType type) {
    switch (type) {
""")
        for enum in enums['values']:
            f.write('        case '+enum['name']+': return "'+enum['name']+'";\n')
        f.write('        default: return "ANARI_UNKNOWN";\n')
        f.write('    }\n')
        f.write('}\n')


        f.write("""
inline const char* typenameOf(ANARIDataType type) {
    switch (type) {
""")
        for enum in enums['values']:
            f.write('        case '+enum['name']+': return "'+enum['baseType']+'";\n')
        f.write('        default: return "ANARI_UNKNOWN";\n')
        f.write('    }\n')
        f.write('}\n')


        f.write("""
inline const char* varnameOf(ANARIDataType type) {
    switch (type) {
""")
        for enum in enums['values']:
            f.write('        case '+enum['name']+': return "var'+enum['name'][6:].lower()+'";\n')
        f.write('        default: return "ANARI_UNKNOWN";\n')
        f.write('    }\n')
        f.write('}\n')


        f.write("""
inline int isNormalized(ANARIDataType type) {
    switch (type) {
""")
        for enum in enums['values']:
            f.write('        case '+enum['name']+': return '+str(int(enum['normalized']))+';\n')
        f.write('        default: return 0;\n')
        f.write('    }\n')
        f.write('}\n')

        f.write('}\n')

output = sys.argv[1]
trees = [json.load(open(x)) for x in sys.argv[2:]]
api = trees[0]
for x in trees[1:]:
    merge_anari.merge(api, x)

filename = os.path.basename(output)
if filename == 'anari.h':
    write_anari_header(output, api)
elif filename == 'anari_enums.h':
    write_anari_enums(output, api)
elif filename == 'type_utility.h':
    write_anari_type_query_helper(output, api)
