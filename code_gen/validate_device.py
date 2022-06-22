# Copyright 2021 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

# this script checks if a device definition is complete

import sys
import os
import json
import hash_gen
import merge_anari
import argparse
import pathlib

def check_feature_completeness(feature, filename):
    if "info" not in feature:
        print("%s is missing info section"%filename)
        return

    info = feature["info"]
    if "name" not in info:
        print("%s info section is missing name"%filename)

    if "type" not in info:
        print("%s info section is missing type"%filename)


def check_object_completeness(device):
    for obj in device["objects"]:
        objectname = ""
        if "type" not in obj:
            if "name" in obj:
                print("object \"%s\" missing type"%obj["name"])
            else:
                print("anonymous object missing type")
        else:
            objectname += obj["type"]

        if "name" in obj:
            objectname += " \"%s\""%obj["name"]

        if "description" not in obj:
            print("%s missing description"%objectname)

        if "parameters" not in obj:
            print("%s: missing parameters"%objectname)
        else:
            for param in obj["parameters"]:
                if "name" not in param:
                    print("%s: parameter missing name"%objectname)

                paramname = param["name"]

                if "types" not in param:
                    print("%s parameter \"%s\" missing types"%(objectname, paramname))
                elif not isinstance(param["types"], list):
                    print("%s parameter \"%s\" types must be array"%(objectname, paramname))
                elif not param["types"]:
                    print("%s parameter \"%s\" types list can't be empty"%(objectname, paramname))

                if "ANARI_ARRAY1D" in param["types"] or "ANARI_ARRAY2D" in param["types"] or "ANARI_ARRAY3D" in param["types"]:
                    if "elementType" not in param:
                        print("%s parameter \"%s\" allows arrays but is missing elementType"%(objectname, paramname))
                    elif not isinstance(param["elementType"], list):
                        print("%s parameter \"%s\" elementType must be an array"%(objectname, paramname))
                    elif not param["elementType"]:
                        print("%s parameter \"%s\" elementType list can't be empty"%(objectname, paramname))

                if "tags" not in param:
                    print("%s parameter \"%s\"  missing tags"%(objectname, paramname))
                elif not isinstance(param["types"], list):
                    print("%s parameter \"%s\" tags must be an array"%(objectname, paramname))

                if "description" not in param:
                    print("%s parameter \"%s\" missing description"%(objectname, paramname))


parser = argparse.ArgumentParser(description="Generate query functions for an ANARI device.")
parser.add_argument("-d", "--device", dest="devicespec", type=open, help="The device json file.")
parser.add_argument("-j", "--json", dest="json", type=pathlib.Path, action="append", help="Path to the core and extension json root.")
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
        check_feature_completeness(feature, m)
        merge_anari.tag_feature(feature)
        merge_anari.merge(device, feature)

check_object_completeness(device)