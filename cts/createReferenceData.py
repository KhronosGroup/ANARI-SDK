#!/usr/bin/env python3
import cts
import json
import argparse
import math

def writeMetaData(sceneGenerator, scene_location, permutationString):
    original_json = {}
    with open(scene_location, "r") as scene_file:
        original_json = json.load(scene_file)
    metaData = {"bounds": {}}
    bounds = sceneGenerator.getBounds()

    isInf = math.isinf(bounds[0][0][0][0])
    metaData["bounds"]["world"] = bounds[0][0] if not isInf else "Infinity"
    if len(bounds[1]) > 0:
        metaData["bounds"]["instances"] = bounds[1] if not isInf else "Infinity"
    if len(bounds[2]) > 0:
        metaData["bounds"]["groups"] = bounds[2] if not isInf else "Infinity"
    if permutationString == "":
        original_json["metaData"] = metaData
    else:
        if "metaData" not in original_json:
            original_json["metaData"] = {}
        original_json["metaData"][permutationString] = metaData
    with open(scene_location, "w") as scene_file:
        json.dump(original_json, scene_file, indent=2)


def cleanMetaData(
    _lib, _dev, _ren, scene_location, _test_name, _permutationString, _variantString
):
    original_json = {}
    with open(scene_location, "r") as scene_file:
        original_json = json.load(scene_file)
    if "metaData" in original_json:
        del original_json["metaData"]
        with open(scene_location, "w") as scene_file:
            json.dump(original_json, scene_file, indent=2)


def createReferenceData(
    parsed_json,
    sceneGenerator,
    anariRenderer,
    sceneLocation,
    test_name,
    permutationString,
    variantString,
):
    cts.render_scene(
        parsed_json,
        sceneGenerator,
        anariRenderer,
        sceneLocation,
        test_name,
        permutationString,
        variantString,
        prefix="ref_",
    )
    writeMetaData(sceneGenerator, sceneLocation, permutationString)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ANARI CTS toolkit generation")
    parser.add_argument("-l", "--library", default="helide")
    parser.add_argument("-d", "--device", default=None)
    parser.add_argument("-r", "--renderer", default="default")
    parser.add_argument("-t", "--test_scenes", default="test_scenes")
    parser.add_argument("--ignore_features", action="store_true")

    args = parser.parse_args()
    cts.apply_to_scenes(
        cleanMetaData, args.library, args.device, args.renderer, args.test_scenes, True, not args.ignore_features
    )
    cts.apply_to_scenes(
        createReferenceData,
        args.library,
        args.device,
        args.renderer,
        args.test_scenes,
        True,
        not args.ignore_features
    )
