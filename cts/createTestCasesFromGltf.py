#!/usr/bin/env python3
from pathlib import Path, PureWindowsPath
import json
import argparse
import os

import anariCTSBackend
import ctsGLTF

required_features = []
optional_features = []
parsing_required_features = False


def record_features(message):
    global parsing_required_features
    global required_features
    global optional_features

    if "used features:" in message:
        # start parsing features
        parsing_required_features = True
        return
    if "ANARI_" in message and parsing_required_features:
        # parse feature
        split_message = message.split("]")
        feature = split_message[1].strip()
        if feature == "ANARI_KHR_MATERIAL_PHYSICALLY_BASED":
            # fallback to matte material means that pbr materials are an optional feature
            optional_features.append(feature)
        else:
            required_features.append(feature)
    else:
        # stop parsing features
        if parsing_required_features:
            parsing_required_features = False


def create_test_cases_from_gltf(gltf_dir, output_path, blacklist=[]):
    # gather all paths to gltf files in dir
    gltf_scenes = (
        gather_sample_assets(gltf_dir, blacklist)
        if "gltf-Sample-Assets" in gltf_dir
        else gather_gltf(gltf_dir, blacklist)
    )
    for gltf_scene in gltf_scenes:
        gltf_scene_name = None
        absolute_gltf_path = Path(__file__).parent / gltf_dir
        relative_gltf_path = None
        if isinstance(gltf_scene, dict):
            for parent, children in gltf_scene.items():
                gltf_scene_name = parent.stem
                # create paths
                relative_gltf_path = os.path.relpath(parent, absolute_gltf_path)
                absolute_json_path = (
                    absolute_gltf_path.parent
                    / Path(output_path)
                    / Path(relative_gltf_path)
                    / Path(parent.stem + ".json")
                )
                # print info
                print_info(parent, absolute_json_path)
                # get glTF info
                gltf = load_gltf(children[0])
                # create test
                json_data = create_test_json(
                    parent.name,
                    [
                        PureWindowsPath(
                            os.path.relpath(child, absolute_json_path.parent)
                        ).as_posix()
                        for child in children
                    ],
                    gltf.json,
                )
        else:
            # create paths
            gltf_scene_name = gltf_scene.stem
            relative_gltf_path = os.path.relpath(gltf_scene.parent, absolute_gltf_path)
            absolute_json_path = (
                absolute_gltf_path.parent
                / Path(output_path)
                / Path(relative_gltf_path)
                / Path(gltf_scene.stem + ".json")
            )
            # print info
            print_info(gltf_scene, absolute_json_path)
            # get glTF info
            gltf = load_gltf(gltf_scene)
            # create test
            json_data = create_test_json(
                gltf_scene.name,
                [
                    PureWindowsPath(
                        os.path.relpath(gltf_scene, absolute_json_path.parent)
                    ).as_posix()
                ],
                gltf.json,
            )
        # fill test case with required/optional features
        query_features(gltf)
        cleanup_required_features(gltf.json)

        json_data["requiredFeatures"] = required_features.copy()
        json_data["optionalFeatures"] = optional_features.copy()
        # clear features for next test case
        required_features.clear()
        optional_features.clear()
        # write test
        output_dir = Path(output_path + "/" + relative_gltf_path)
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
        with open(
            output_dir / Path(gltf_scene_name + ".json", encoding="utf-8"), "w"
        ) as f:
            json.dump(json_data, f, indent=2)


def query_features(gltf):
    # use this in case of global scene generator
    # sceneGenerator.resetAllParameters()
    sceneGenerator = anariCTSBackend.SceneGenerator(record_features)

    sceneGenerator.loadGLTF(
        json.dumps(gltf.json), gltf.buffers, gltf.images, False, True
    )
    _ = sceneGenerator.renderScene(0.0)


def cleanup_required_features(gltfJson):
    # remove material requirements if no materials are present
    if not "materials" in gltfJson or not gltfJson["materials"]:
        if "ANARI_KHR_MATERIAL_PHYSICALLY_BASED" in required_features:
            required_features.remove("ANARI_KHR_MATERIAL_PHYSICALLY_BASED")
    # remove certain requirements that are prerequisites for the whole test suite
    if "ANARI_KHR_MATERIAL_MATTE" in required_features:
        required_features.remove("ANARI_KHR_MATERIAL_MATTE")
    if "ANARI_KHR_MATERIAL_PHYSICALLY_BASED" in required_features:
        required_features.remove("ANARI_KHR_MATERIAL_PHYSICALLY_BASED")
    if "ANARI_KHR_GEOMETRY_TRIANGLE" in required_features:
        required_features.remove("ANARI_KHR_GEOMETRY_TRIANGLE")
    if "ANARI_KHR_CAMERA_PERSPECTIVE" in required_features:
        required_features.remove("ANARI_KHR_CAMERA_PERSPECTIVE")
    if "ANARI_KHR_RENDERER_BACKGROUND_COLOR" in required_features:
        required_features.remove("ANARI_KHR_RENDERER_BACKGROUND_COLOR")
    if "ANARI_KHR_RENDERER_AMBIENT_LIGHT" in required_features:
        required_features.remove("ANARI_KHR_RENDERER_AMBIENT_LIGHT")


def gather_gltf(gltf_dir, blacklist=[]):
    absolute_path = Path(Path(__file__).parent / gltf_dir)
    if absolute_path.is_dir():
        gltf_files = list(absolute_path.rglob("*.gltf"))
        glb_files = list(absolute_path.rglob("*.glb"))
        collected_gltf = gltf_files + glb_files
        collected_gltf = [
            x for x in collected_gltf if (not any(term in str(x) for term in blacklist))
        ]
        return collected_gltf
    else:
        print("glTF path must lead to a directory")
        return []


def gather_sample_assets(gltf_dir, blacklist=[]):
    blacklist.extend(
        ["glTF-Quantized", "Sparse", "glTF-Embedded", "glTF-Meshopt", "glTF-IBL"]
    )
    gltfs = gather_gltf(gltf_dir, blacklist)
    current_parent = None
    collected_gltfs = []
    current_collected_gltfs = []
    hasGlb = False
    gltfs.sort()
    for gltf in reversed(gltfs):
        if current_parent != gltf.parent.parent:
            if current_parent != None:
                collected_gltfs.append({current_parent: current_collected_gltfs})
            current_collected_gltfs = []
            current_parent = gltf.parent.parent
            hasGlb = False
        if gltf.parent.stem == "glTF-Binary":
            hasGlb = True
        elif gltf.suffix == ".gltf" and hasGlb:
            continue
        current_collected_gltfs.append(gltf)
    collected_gltfs.append({current_parent: current_collected_gltfs})

    return collected_gltfs


def create_test_json(name, paths, gltf_json):
    camera_count = get_camera_count(gltf_json)
    if len(paths) > 1:
        json_data = {
            "description": "Test " + str(name),
            "sceneParameters": {"primitiveCount": 0},
            "permutations": {"/gltf/file": paths},
        }
        if camera_count > 0:
            cameras = list(range(0, camera_count))
            cameras.insert(0, None)
            json_data["permutations"]["/gltf/camera"] = cameras
        return json_data

    json_data = {
        "description": "Test " + str(name),
        "sceneParameters": {"primitiveCount": 0, "gltf": {"file": str(paths[0])}},
    }
    if camera_count > 0:
        cameras = list(range(0, camera_count))
        cameras.insert(0, None)
        json_data["permutations"] = {"/gltf/camera": cameras}
    return json_data


def load_gltf(gltf_path):
    gltf = (
        ctsGLTF.loadGLB(gltf_path)
        if gltf_path.suffix == ".glb"
        else ctsGLTF.loadGLTF(gltf_path)
    )
    return gltf


def get_camera_count(gltf_json):
    if "cameras" in gltf_json:
        return len(gltf_json["cameras"])
    return 0


def print_info(source_path, test_path):
    print("Creating test case")
    print(f"    Source glTF: " + str(source_path))
    print(f"    Test case: " + str(test_path))
    print("")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ANARI CTS toolkit generation")
    parser.add_argument("-g", "--gltf", default="./gltf-Sample-Assets")
    parser.add_argument("-o", "--output", default="test_scenes/gltf")
    parser.add_argument("-b", "--blacklist", nargs="+", default=[])

    args = parser.parse_args()
    create_test_cases_from_gltf(args.gltf, args.output, args.blacklist)
