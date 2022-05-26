import sys
import os
import json

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
        c = next(x for x in core if x['name'] == name)
        if c:
            check_conflicts(c['values'], enum['values'], 'name', name)
            check_conflicts(c['values'], enum['values'], 'value', name)
            c['values'].extend(enum['values'])
        else:
            core.extend(extension)

def merge(core, extension):
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



def write_anari_enum_list(anari):
    f = open("enums.txt", mode='w')
    # generate enums
    for enum in anari['enums']:
        for value in enum['values']:
            f.write(value['name']+'\n')

def write_anari_function_list(anari):
    f = open("functions.txt", mode='w')
    # generate enums
    for func in anari['functions']:
        f.write(func['name']+'\n')

def write_anari_type_list(anari):
    f = open("types.txt", mode='w')
    # generate enums
    for x in anari['enums']:
        f.write(x['name']+'\n')
    for x in anari['opaqueTypes']:
        f.write(x['name']+'\n')
    for x in anari['functionTypedefs']:
        f.write(x['name']+'\n')
    for x in anari['structs']:
        f.write(x['name']+'\n')

trees = [json.load(open(x)) for x in sys.argv[1:]]
api = trees[0]
for x in trees[1:]:
    merge(api, x)


write_anari_enum_list(api)
write_anari_function_list(api)
write_anari_type_list(api)