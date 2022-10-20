import cts
import json
import argparse

def writeMetaData(sceneGenerator, scene_location, permutationString):
    original_json = {}
    with open(scene_location, 'r') as scene_file:
        original_json = json.load(scene_file)
    metaData = {}
    metaData["bounds"] = sceneGenerator.getBounds()
    frame_duration = sceneGenerator.getFrameDuration()
    if frame_duration <= 0 :
        print(f'Frame duration invalid: {frame_duration}')
    if "metaData" not in original_json:
        original_json["metaData"] = {}
    original_json["metaData"][permutationString] = metaData
    with open(scene_location, 'w') as scene_file:
        json.dump(original_json, scene_file, indent=2)

def cleanMetaData(_lib, _dev, _ren, scene_location, _):
    original_json = {}
    with open(scene_location, 'r') as scene_file:
        original_json = json.load(scene_file)
    if "metaData" in original_json:
        del original_json["metaData"]
        with open(scene_location, 'w') as scene_file:
            json.dump(original_json, scene_file, indent=2)


def createReferenceData(parsed_json, sceneGenerator, anariRenderer, sceneLocation, permutationString):
    cts.render_scene(parsed_json, sceneGenerator, anariRenderer, sceneLocation, permutationString, prefix="ref_")
    writeMetaData(sceneGenerator, sceneLocation, permutationString)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ANARI CTS toolkit generation')
    parser.add_argument("--test_scenes", default="test_scenes")

    args = parser.parse_args()
    cts.apply_to_scenes(cleanMetaData, "example", "example", "default", args.test_scenes)
    cts.apply_to_scenes(createReferenceData, "example", "example", "default", args.test_scenes)
