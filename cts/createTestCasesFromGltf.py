#!/usr/bin/env python3
from pathlib import Path
import json
import argparse
import os

def create_test_cases_from_gltf(gltf_dir, output_path):
    # gather all paths to gltf files in dir
    gltf_scenes = gather_gltf(gltf_dir)
    for gltf_scene in gltf_scenes:
        # create paths
        absolute_gltf_path = Path(__file__).parent / gltf_dir
        relative_gltf_path = os.path.relpath(gltf_scene.parent, absolute_gltf_path)
        # print info
        print("Creating test case")
        print(f"    Source glTF: " + str(gltf_scene))
        print(f"    Test case: " + str(absolute_gltf_path.parent / Path(output_path) / Path(relative_gltf_path) / Path(gltf_scene.stem + '.json')))
        print("")
        # create test
        json_data = create_test_json(gltf_scene)
        # write test
        output_dir = Path(output_path + "/" + relative_gltf_path)
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
        with open(output_dir / Path(gltf_scene.stem + '.json'), 'w') as f:
            json.dump(json_data, f, indent=2)

def gather_gltf(gltf_dir):
    absolute_path = Path(Path(__file__).parent / gltf_dir)
    if absolute_path.is_dir():
        gltf_files = list(absolute_path.rglob("*.gltf"))
        glb_files = list(absolute_path.rglob("*.glb"))
        collected_gltf = gltf_files + glb_files
        return collected_gltf
    else:
        print("glTF path must lead to a directory")
        return []

def create_test_json(gltf_scene):
    json_data = {
        "description": "Test " + str(gltf_scene.name),
        "sceneParameters": {
            "primitiveCount": 0,
            "gltf": str(gltf_scene)
        }
    }
    return json_data

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ANARI CTS toolkit generation")
    parser.add_argument("-g", "--gltf", default="./glTF-Sample-Models")
    parser.add_argument("-o", "--output", default="test_scenes/gltf")

    args = parser.parse_args()
    create_test_cases_from_gltf(args.gltf, args.output)
