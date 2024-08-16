#!/usr/bin/env python3
from pathlib import Path
import json
import argparse
import os

# TODO add support for non-directories
def create_test_cases_from_gltf(gltf_path, output_path):
    print(gltf_path)
    gltf_scenes = gather_gltf(gltf_path)
    for gltf_scene in gltf_scenes:
        json_data = create_test_json(gltf_scene)
        relative_path = os.path.relpath(gltf_scene.parent, Path(Path(__file__).parent / gltf_path))
        print("last_mile: " + relative_path)
        print("glTF: " + str(gltf_scene))
        if not os.path.exists(output_path + "/" + relative_path):
            os.makedirs(output_path + "/" + relative_path)
        with open(output_path + "/" + relative_path + "/" + gltf_scene.stem + '.json', 'w') as f:
            json.dump(json_data, f, indent=2)

def gather_gltf(gltf_scenes):
    collected_gltf = []
    if isinstance(gltf_scenes, list):
        # gltf_scenes is a list of paths
        for gltf_scene in gltf_scenes:
            path = Path(gltf_scene)
            if path.exists():
                collected_gltf.append(path)
            else:
                print(f'Path does not exist: {str(path)}')
    else:
        path = Path(Path(__file__).parent / gltf_scenes)
        if not path.is_dir():
            # gltf_scenes is a file
            if path.suffix == '.gltf' | path.suffix == '.glb':
                return [path]
            print("No valid category")
            return []
        else:
            # gltf_scenes is a directory
            gltf_files = list(path.rglob("*.gltf"))
            glb_files = list(path.rglob("*.glb"))
            collected_gltf = gltf_files + glb_files
    return collected_gltf

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
    parser.add_argument("-g", "--gltf", default=".")
    parser.add_argument("-o", "--output", default="test_scenes/gltf")

    args = parser.parse_args()
    create_test_cases_from_gltf(args.gltf, args.output)
