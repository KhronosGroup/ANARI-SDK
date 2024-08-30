#!/usr/bin/env python3
from pathlib import Path, PureWindowsPath
import json
import argparse
import os

def create_test_cases_from_gltf(gltf_dir, output_path, blacklist = []):
    # gather all paths to gltf files in dir
    gltf_scenes = gather_sample_assets(gltf_dir, blacklist) if "gltf-Sample-Assets" in gltf_dir  else gather_gltf(gltf_dir, blacklist)
    for gltf_scene in gltf_scenes:
        gltf_scene_name = None
        absolute_gltf_path = Path(__file__).parent / gltf_dir
        relative_gltf_path = None
        if (isinstance(gltf_scene, dict)):
            for parent, children in gltf_scene.items():
                gltf_scene_name = parent.stem
                # create paths
                relative_gltf_path = os.path.relpath(parent, absolute_gltf_path)
                absolute_json_path = absolute_gltf_path.parent / Path(output_path) / Path(relative_gltf_path) / Path(parent.stem + '.json')
                # print info
                print("Creating test case")
                print(f"    Source glTF: " + str(parent))
                print(f"    Test case: " + str(absolute_json_path))
                print("")
                # create test
                json_data = create_test_json(parent.name, [PureWindowsPath(os.path.relpath(child, absolute_json_path.parent)).as_posix() for child in children])
        else:
            # create paths
            gltf_scene_name = gltf_scene.stem
            relative_gltf_path = os.path.relpath(gltf_scene.parent, absolute_gltf_path)
            absolute_json_path = absolute_gltf_path.parent / Path(output_path) / Path(relative_gltf_path) / Path(gltf_scene.stem + '.json')
            # print info
            print("Creating test case")
            print(f"    Source glTF: " + str(gltf_scene))
            print(f"    Test case: " + str(absolute_json_path))
            print("")
            # create test
            json_data = create_test_json(gltf_scene.name, [PureWindowsPath(os.path.relpath(gltf_scene, absolute_json_path.parent)).as_posix()])
        # write test
        output_dir = Path(output_path + "/" + relative_gltf_path)
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
        with open(output_dir / Path(gltf_scene_name + '.json'), 'w') as f:
            json.dump(json_data, f, indent=2)

def gather_gltf(gltf_dir, blacklist = []):
    absolute_path = Path(Path(__file__).parent / gltf_dir)
    if absolute_path.is_dir():
        gltf_files = list(absolute_path.rglob("*.gltf"))
        glb_files = list(absolute_path.rglob("*.glb"))
        collected_gltf = gltf_files + glb_files
        collected_gltf = [x for x in collected_gltf if (not any(term in str(x) for term in blacklist))]
        return collected_gltf
    else:
        print("glTF path must lead to a directory")
        return []
    
def gather_sample_assets(gltf_dir, blacklist = []):
    blacklist.extend(["glTF-Quantized", "Sparse", "glTF-Embedded", "glTF-Meshopt", "glTF-IBL"])
    gltfs = gather_gltf(gltf_dir, blacklist)
    current_parent = None
    collected_gltfs = []
    current_collected_gltfs = []
    hasGlb = False
    for gltf in reversed(gltfs):
        if current_parent != gltf.parent.parent:
            if current_parent != None:
                collected_gltfs.append({current_parent: current_collected_gltfs})
            current_collected_gltfs = []
            current_parent = gltf.parent.parent
            hasGlb = False
        if (gltf.parent.stem == "glTF"):
            hasGlb = True
        elif (gltf.suffix == ".gltf" and hasGlb):
            continue
        current_collected_gltfs.append(gltf)

    return collected_gltfs
        


def create_test_json(name, paths):
    if len(paths) > 1:
        json_data = {
            "description": "Test " + str(name),
            "sceneParameters": {
                "primitiveCount": 0
            },
            "permutations" : {
                "gltf" : paths
            }
        }
        return json_data

    json_data = {
        "description": "Test " + str(name),
        "sceneParameters": {
            "primitiveCount": 0,
            "gltf": str(paths[0])
        }
    }
    return json_data

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ANARI CTS toolkit generation")
    parser.add_argument("-g", "--gltf", default="./gltf-Sample-Assets")
    parser.add_argument("-o", "--output", default="test_scenes/gltf")
    parser.add_argument("-b", "--blacklist", nargs="+", default=[])

    args = parser.parse_args()
    create_test_cases_from_gltf(args.gltf, args.output, args.blacklist)
