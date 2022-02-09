# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

def check_conflicts(a, b, key, scope):
    a_set = set([x[key] for x in a])
    b_set = set([x[key] for x in b])
    conflicts = set.intersection(a_set, b_set)
    for conflict in conflicts:
        entries = [x for x in a if x[key] == conflict] + [x for x in b if x[key] == conflict]
        print('duplicate '+key+' while merging '+scope)
        for e in entries:
            print(e)

def merge_enums(core, extension):
    for enum in extension:
        name = enum['name']
        c = next((x for x in core if x['name'] == name), None)
        if c:
            check_conflicts(c['values'], enum['values'], 'name', name)
            check_conflicts(c['values'], enum['values'], 'value', name)
            c['values'].extend(enum['values'])
        else:
            core.extend(extension)

def merge_api(core, extension):
    for k in extension.keys():
        if k == "enums" :
            merge_enums(core[k], extension[k])
        elif k == "info":
            print('merging '+extension[k]['type']+' '+extension[k]["name"])
        elif k == "descriptions":
            core.extend(extension)
        else:
            check_conflicts(core[k], extension[k], 'name', k)
            core[k].extend(extension[k])

def merge_parameters(core, extension):
    for param in extension:
        name = param["name"]
        c = next((x for x in core if x["name"] == name), None)
        if c:
            c["types"].extend(param["types"])
            if "values" in c:
                c["values"].extend(param["values"])
        else:
            core.append(param)

def merge_object_table(core, extension):
    for obj in extension:
        if "name" in obj:
            name = obj["name"]
            kind = obj["type"]
            c = next((x for x in core if x["type"] == kind and "name" in x and x["name"] == name), None)
            if c:
                merge_parameters(c["parameters"], obj["parameters"])
            else:
                core.append(obj)
        else:
            kind = obj["type"]
            c = next((x for x in core if x["type"] == kind), None)
            if c:
                merge_parameters(c["parameters"], obj["parameters"])
            else:
                core.append(obj)

def merge(core, extension):
    for k,v in extension.items():
        if not k in core:
            core[k] = v
        elif k == "info":
            print('merging '+extension[k]['type']+' '+extension[k]["name"])
        elif k == "enums" :
            merge_enums(core[k], extension[k])
        elif k == "descriptions":
            core.extend(extension)
        elif k == "objects":
            merge_object_table(core[k], extension[k])
        else:
            check_conflicts(core[k], extension[k], 'name', k)
            core[k].extend(extension[k])
