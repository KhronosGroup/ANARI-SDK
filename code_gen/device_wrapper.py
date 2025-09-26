# Copyright 2021-2025 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

license = '''# Copyright 2021-2025 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

'''

boilerplate = '''
@ffi.def_extern()
def ANARIStatusCallback_python(usrPtr, device, source, sourceType, severity, code, message):
    cb = ffi.from_handle(usrPtr)
    cb(device, source, sourceType, severity, code, ffi.string(message).decode('utf-8'))

'''

conversions = '''
def _from_string_list(x):
    result = []
    if x != ffi.NULL:
        i = 0
        while x[i] != ffi.NULL:
            result.append(str(ffi.string(x[i]), 'utf-8'))
            i += 1
    return result

def _from_type_list(x):
    result = []
    if x != ffi.NULL:
        i = 0
        while x[i] != lib.ANARI_UNKNOWN:
            result.append(x[i])
            i += 1
    return result

def _from_parameter_list(x):
    result = []
    if x != ffi.NULL:
        i = 0
        while x[i].name != ffi.NULL:
            result.append((str(ffi.string(x[i].name), 'utf-8'), x[i].type))
            i += 1
    return result

def _convert_pointer(dataType, x):
    if x == ffi.NULL:
        return None

    x = ffi.cast(_basepointer[dataType], x)

    if dataType == lib.ANARI_STRING_LIST:
        return _from_string_list(x)
    elif dataType == lib.ANARI_DATA_TYPE_LIST:
        return _from_type_list(x)
    elif dataType == lib.ANARI_PARAMETER_LIST:
        return _from_parameter_list(x)
    elif dataType == lib.ANARI_STRING:
        return str(ffi.string(x), 'utf-8')
    elif _elements[dataType]==1:
        return x[0]
    elif _elements[dataType]==2:
        return (x[0], x[1])
    elif _elements[dataType]==3:
        return (x[0], x[1], x[2])
    elif _elements[dataType]==4:
        return (x[0], x[1], x[2], x[3])
    else:
        return ptr

'''

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


def write_wrappers(anari, name):
    code = license
    code += 'from .' + name + ' import lib, ffi\n'
    code += boilerplate
    
    for enum in anari['enums']:
        for value in enum['values']:
            code += value['name'][6:]+' = lib.'+value['name']+'\n'
        code += '\n'

    types = next((x for x in anari['enums'] if x['name'] == 'ANARIDataType'), None)

    code += '_conversions = {\n'
    for value in types['values']:
        if value['elements'] == 1:
            code += '    %s : \'%s*\',\n'%(value['name'][6:], value['baseType'])
        else:
            code += '    %s : \'%s[%d]\',\n'%(value['name'][6:], value['baseType'], value['elements'])
    code += '}\n'

    code += '_typeof = {\n'
    for value in types['values']:
        if value['elements'] == 1:
            code += '    %s : \'%s\',\n'%(value['name'][6:], value['baseType'])
        else:
            code += '    %s : \'%s[%d]\',\n'%(value['name'][6:], value['baseType'], value['elements'])
    code += '}\n'

    code += '_basepointer = {\n'
    for value in types['values']:
        if value['baseType'][-1] == '*':
            code += '    %s : \'%s\',\n'%(value['name'][6:], value['baseType'])
        else:
            code += '    %s : \'%s*\',\n'%(value['name'][6:], value['baseType'])
    code += '}\n'

    code += '_elements = {\n'
    for value in types['values']:
        code += '    %s : %s,\n'%(value['name'][6:], value['elements'])
    code += '}\n'

    code += conversions

    code += '\n'

    return code
