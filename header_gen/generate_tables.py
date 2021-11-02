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
        else:
            check_conflicts(core[k], extension[k], 'name', k)
            core[k].extend(extension[k])



def write_anari_enum_table(filename, anari, descriptions):
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    f = open(filename, mode='w')

    f.write('|Name |Type |Description\n\n')
    # generate enums
    for enum in anari['enums']:
        if enum['name'] == 'ANARIDataType':
            for value in enum['values']:
                if value['name'] == 'ANARI_UNKNOWN':
                    continue
                f.write('|`'+value['name'][6:]+'`\n') # skip prefix
                if value['elements'] > 1:
                    f.write('|`'+value['baseType']+'['+str(value['elements'])+']`\n')
                else:
                    f.write('|`'+value['baseType']+'`\n')
                f.write('|'+descriptions[value['name']]+'\n\n')




output = sys.argv[1]
descriptions = json.load(open(sys.argv[2]))
trees = [json.load(open(x)) for x in sys.argv[3:]]
api = trees[0]
for x in trees[1:]:
    merge(api, x)

filename = os.path.basename(output)
if filename == 'type_table.txt':
    write_anari_enum_table(output, api, descriptions)
