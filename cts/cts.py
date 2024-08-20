#!/usr/bin/env python3
import anariCTSBackend
try:
    import ctsReport
except ImportError:
    print("Unable to import pdf tool. Only printing to console is supported")
import argparse
import threading
import datetime
from pathlib import Path
from tabulate import tabulate
from PIL import Image
import json
import itertools
import ctsUtility
import glob
import fnmatch
import math
import os

import ctsGLTF

class terminalColors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    def warning(text):
        return f'{terminalColors.WARNING}{text}{terminalColors.ENDC}'
    def error(text):
        return f'{terminalColors.FAIL}{text}{terminalColors.ENDC}'
    def success(text):
        return f'{terminalColors.OKGREEN}{text}{terminalColors.ENDC}'
    def info(text):
        return f'{terminalColors.OKBLUE}{text}{terminalColors.ENDC}'
class terminalColors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    def warning(text):
        return f'{terminalColors.WARNING}{text}{terminalColors.ENDC}'
    def error(text):
        return f'{terminalColors.FAIL}{text}{terminalColors.ENDC}'
    def success(text):
        return f'{terminalColors.OKGREEN}{text}{terminalColors.ENDC}'
    def info(text):
        return f'{terminalColors.OKBLUE}{text}{terminalColors.ENDC}'

logger_mutex = threading.Lock()

reference_prefix = "ref_"

log_file_name = "ANARI.log"
log_file_path = log_file_name

# check whether a specific feature is listed as supported in a device's feature list
def check_feature(feature_list, check_feature):
    for [feature, is_available] in feature_list:
        if feature == check_feature:
            return is_available
    return False

# returns whether a color and/or depth channel is requested in the test scene json
def get_channels(parsed_json):
    channels = []
    if "sceneParameters" in parsed_json:
        scene_parameters = parsed_json["sceneParameters"]
        if "frame_color_type" in scene_parameters and scene_parameters["frame_color_type"] != "":
            channels.append("color")
        if "frame_depth_type" in scene_parameters and scene_parameters["frame_depth_type"] != "":
            channels.append("depth")
    return channels

# appends a message to ANARI.log file
def anari_logger(message):
    with logger_mutex:
        with open(log_file_path, 'a') as file:
            file.write(f'{str(datetime.datetime.now())}: {message}\n')

# queries and returns feature support list of a device
def query_features(anari_library, anari_device = None):
    try:
        return anariCTSBackend.query_features(anari_library, anari_device, anari_logger)
    except Exception as e:
        print(e)
        return []

# recursively apply an update to a dictionary
def recursive_update(d, merge_dict):
    if not isinstance(d, dict) or not isinstance(merge_dict, dict):
        return d
    for key, value in d.items():
        if isinstance(value, dict) and key in merge_dict:
            merge_dict[key] = recursive_update(value, merge_dict[key])
    d.update(merge_dict)
    return d

# gather image paths from a filesystem directory with or without certain prefixes
def globImages(directory, prefix = "", exclude_prefix = ""):
    path_list = glob.glob(f'{str(directory)}/**/{prefix}*.png', recursive = True)
    if exclude_prefix != "":
        path_list = [path for path in path_list if not Path(path).name.startswith(exclude_prefix)]
    return path_list

# returns a path from a list of paths, searches for a filename
def getFileFromList(list, filename):
    for path in list:
        if Path(path).stem == filename:
            return path
    return ""

def simplifyFileName(name):
    name = name.replace('{', '')
    name = name.replace('}', '')
    name = name.replace(':', '')
    name = name.replace("'", '')
    name = name.replace('"', '')
    name = name.replace(' ', '')
    return name

# writes all reference, candidate, diff and threshold images to filesystem and returns
def write_images(evaluations, output):
    output_path = Path(output) / "evaluation"
    for evaluationKey, evaluation in evaluations.items():
        for stem, test in evaluation.items():
            for name, value in test.items():
                if isinstance(value, dict):
                    for channel, channelValue in value.items():
                        if isinstance(channelValue, dict):
                            if "missingImage" in evaluation[stem][name][channel] and evaluation[stem][name][channel]["missingImage"]:
                                continue

                            evaluation[stem][name][channel]["image_paths"] = {}

                            # save the input images to the output directory
                            reference_image_path = Path("reference") / f"{name}_{channel}.png"
                            ctsUtility.write_image(output_path / reference_image_path, evaluation[stem][name][channel]["images"]["reference"])
                            evaluation[stem][name][channel]["image_paths"]["reference"] = reference_image_path

                            candidate_image_path = Path("candidate") / f"{name}_{channel}.png"
                            ctsUtility.write_image(output_path / candidate_image_path, evaluation[stem][name][channel]["images"]["candidate"])
                            evaluation[stem][name][channel]["image_paths"]["candidate"] = candidate_image_path

                            # save the diff image
                            diff_image_path = Path("diffs") / f"{name}_{channel}.png"
                            ctsUtility.write_image(output_path / diff_image_path, evaluation[stem][name][channel]["images"]["diff"], check_contrast=False)
                            evaluation[stem][name][channel]["image_paths"]["diff"] = diff_image_path

                            # save the threshold image
                            thresholds_image_path = Path("thresholds") / f"{name}_{channel}.png"
                            ctsUtility.write_image(output_path / thresholds_image_path, evaluation[stem][name][channel]["images"]["threshold"], check_contrast=False)
                            evaluation[stem][name][channel]["image_paths"]["threshold"] = thresholds_image_path
    return evaluations

# set title and call function to write out pdf report
def write_report(results, output, check_features = True, verbosity = 0):
    output_path = Path(output) / "evaluation"
    title = f'CTS - Report' if "renderer" not in results else f'CTS - Library: {results["library"]} Device: {results["device"]} Renderer: {results["renderer"]}'
    ctsReport.generate_report_document(results, output_path, title, check_features, verbosity)

# compare candidate and reference images using build in or custom compare functions
# returns dictionary of evaluation results per channel per permutation per test scene
def evaluate_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output = ".", ref_files = [], candidate_files = [], methods = ["ssim"], thresholds = None, custom_compare_function = None):
    results = {}
    stem = scene_location.stem
    results[test_name] = {}
    # get required features from test scene json
    if "requiredFeatures" in parsed_json:
        results[test_name]["requiredFeatures"] = parsed_json["requiredFeatures"]
    # find which channel(s) to evaluate
    channels = get_channels(parsed_json)

    # construct permutation/variant dependent name for result dictionary
    if permutationString != "":
        permutationString = f'_{permutationString}'

    if variantString != "":
        variantString = f'_{variantString}'

    name = f'{stem}{permutationString}{variantString}'
    results[test_name][name] = {}

    color_thresholds = []
    parsedThresholds = {}
    if "thresholds" in parsed_json and isinstance(parsed_json["thresholds"], dict) and parsed_json["thresholds"]:
        parsedThresholds = parsed_json["thresholds"]
        methods = parsedThresholds.keys()
        color_thresholds = parsedThresholds.values()

    # evaluate once per channel
    for channel in channels:
        methodsCopy = methods.copy()
        reference_file = f'{reference_prefix}{stem}{permutationString}_{channel}'
        candidate_file = f'{stem}{permutationString}{variantString}_{channel}'

        ref_path = getFileFromList(ref_files, reference_file)
        candidate_path = getFileFromList(candidate_files, candidate_file)
        if ref_path == "":
            print('No reference image for filepath {} could be found.'.format(reference_file))
            continue

        if candidate_path == "":
            print('No candidate images for filepath {} could be found.'.format(candidate_file))
            results[str(test_name)][name][channel] = {
                "missingImage": True
            }
            continue

        if channel == "depth":
            # only use psnr as comparison method for depth tests
            methodsCopy = ["psnr"]
            channelThresholds = [20.0]
            custom_compare_function = None
        else:
            # color channels might have multiple thresholds for multiple comparison methods
            channelThresholds = color_thresholds
        # evaluate rendered scene against reference data (possibly using a custom comparison function)
        eval_result = ctsUtility.evaluate_scene(ref_path, candidate_path, methodsCopy, channelThresholds, custom_compare_function)
        # print(f'\n{test_name} {name} {channel}:')
        
        # print general information about the test to console
        info = query_scene_info(parsed_json)
        print_scene_info(f'{test_name} {name} {channel}:', info)

        for key in eval_result["metrics"]:
            # construct report text for passed or failed tests
            hasPassed = "\033[92mPassed\033[0m" if eval_result["passed"][key] else "\033[91mFailed\033[0m"
            print(f'   {key}: {hasPassed} {eval_result["metrics"][key]} Threshold: {eval_result["thresholds"][key]}')
        # integrate evaluation for this channel into results dictionary
        results[str(test_name)][name][channel] = eval_result
    return results

# get paths to test scene json from a list or recursively from a directory
def resolve_scenes(test_scenes):
    collected_scenes = []
    if isinstance(test_scenes, list):
        # test_scenes is a list of paths
        for test_scene in test_scenes:
            path = Path(test_scene)
            if path.exists():
                collected_scenes.append(path)
            else:
                print(f'Path does not exist: {str(path)}')
    else:
        # test_scenes is a directory
        if test_scenes != "" and (test_scenes[0] == "/" or test_scenes[0] == "\\"):
            test_scenes = test_scenes[1:]
        path = Path(__file__).parent
        if (Path(path / test_scenes).is_dir()):
            test_scenes += "/**/*.json"
        collected_scenes = list(path.glob(test_scenes))
        collected_scenes = fnmatch.filter(collected_scenes, "*.json")
        if collected_scenes == []:
            print("No test scenes found")
    return collected_scenes

# renders a scene to filesystem that has been set up previously using a sceneGenerator
# returns frame duration of that rendering operation
def render_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output = ".", prefix = ""):
    world_bounds = []
    # get world bounds if they already exist in test scene json
    if "metaData" in [parsed_json]:
        metaData = parsed_json["metaData"]
        if permutationString != "":
            metaData = metaData[permutationString]
        if "bounds" in metaData and "world" in metaData["bounds"]:
            world_bounds = metaData["bounds"]["world"]

    # alternatively get world bounds from currently set up scene in sceneGenerator
    if world_bounds == []:
        try:
            world_bounds = sceneGenerator.getBounds()[0][0]
        except Exception as e:
            print(e)
            return -1

    # render test scene and get frame duration of that rendering
    bounds_distance = math.dist(world_bounds[0], world_bounds[1])
    try:
        image_data_list = sceneGenerator.renderScene(bounds_distance)
        frame_duration = sceneGenerator.getFrameDuration()
    except Exception as e:
        print(e)
        return -1
    
    if permutationString != "":
        permutationString = f'_{permutationString}'

    if variantString != "":
        permutationString += f'_{variantString}'

    # print general information about the test to console
    info = query_scene_info(parsed_json)
    print_scene_info(f'{test_name}{permutationString}', info)

    print(f'   Frame duration: {frame_duration}')

    # construct file names for rendered channels
    output_path = Path(output)

    file_name = output_path / Path(test_name)

    stem = scene_location.stem
    channels = get_channels(parsed_json)

    file_name.parent.mkdir(exist_ok=True, parents=True)

    # construct images from rendered data and write to filesystem
    if "color" in channels and image_data_list[0]:
        image_out = Image.new("RGBA", (parsed_json["sceneParameters"]["image_height"], parsed_json["sceneParameters"]["image_width"]))
        image_out.putdata(image_data_list[0])
        outName = file_name.with_suffix('.png').with_stem(f'{prefix}{stem}{permutationString}_color')
        print(f'   Rendering to {outName.resolve()}')
        image_out.save(outName)

    if "depth" in channels and image_data_list[1]:
        image_out = Image.new("RGBA", (parsed_json["sceneParameters"]["image_height"], parsed_json["sceneParameters"]["image_width"]))
        image_out.putdata(image_data_list[1])
        outName = file_name.with_suffix('.png').with_stem(f'{prefix}{stem}{permutationString}_depth')
        print(f'   Rendering to {outName.resolve()}')
        image_out.save(outName)

    print("")
    return frame_duration

def passByType(paramName, type, paramValue, sceneGenerator):
    if paramValue == None:
        sceneGenerator.unsetGenericParameter(paramName)
    elif type == "Array1D":
        sceneGenerator.setGenericArray1DParameter(paramName, paramValue)
    elif type == "Array2D":
        sceneGenerator.setGenericArray2DParameter(paramName, paramValue)
    elif type == "Texture2D":
        sceneGenerator.setGenericTexture2D(paramName, paramValue)
    else:
        sceneGenerator.setGenericParameter(paramName, paramValue)

# applies a function to each test scene (or test permutation), passing additional args to that function
# returns a dictonary of return values of the passed function with the test scene names as keys
def apply_to_scenes(func, anari_library, anari_device = None, anari_renderer = "default", test_scenes = "test_scenes", only_permutations = False, check_features = True, use_generator = True,  *args):
    result = {}
    # gather available test scenes
    collected_scenes = resolve_scenes(test_scenes)
    if collected_scenes == []:
        print("No scenes selected")
        return result

    # create SceneGenerator if required (only needed for rendering)
    sceneGenerator = None
    if use_generator:
        try:
            sceneGenerator = anariCTSBackend.SceneGenerator(anari_library, anari_device, anari_logger)
        except Exception as e:
            print(e)
            return result
        print('Initialized scene generator\n')
        feature_list = query_features(anari_library, anari_device)

    # loop over test scenes
    for json_file_path in collected_scenes:
        # parse test scene json files
        test_name = json_file_path.name
        scene_location_parts = json_file_path.parts

        if "test_scenes" in scene_location_parts:
            test_scenes_index = scene_location_parts[::-1].index("test_scenes")
            test_name = str(Path(*(scene_location_parts[len(scene_location_parts) - test_scenes_index - 1:])).with_suffix(""))

        parsed_json = {}
        with open(json_file_path, 'r') as f, open('default_test_scene.json', 'r') as defaultTestScene:
            parsed_json = json.load(defaultTestScene)
            parsed_json = recursive_update(parsed_json, json.load(f))

        if use_generator:
            # check whether the device supports all features necessary to render this scene
            all_features_available = True
            if "requiredFeatures" in parsed_json:
                for feature in parsed_json["requiredFeatures"]:
                    if not check_feature(feature_list, feature):
                        all_features_available = False
                        print("Feature %s is not supported"%feature)

            if not all_features_available and check_features:
                # skip this test scene if a required feature is missing
                print("Scene %s is not supported"%json_file_path)
                result[test_name] = "Features not supported"
                continue
            try:
                # setup sceneGenerator with scene parameters for rendering
                sceneGenerator.resetAllParameters()
                references = []
                for [key, value] in parsed_json["sceneParameters"].items():
                    if key == "anari_objects":
                        for [anariObjectName, array] in value.items():
                            for idx, item in enumerate(array):
                                subtype = None
                                if "subtype" not in item:
                                    # Renderer subtype is user-defined/instance, group and surface do not have a subtype
                                    if anariObjectName == "renderer" or anariObjectName == "instance" or anariObjectName == "group" or anariObjectName == "surface":
                                        subtype = anari_renderer
                                    else:
                                        # If no subtype is present no object is generated. This can be used prevent the initialization of default scene objects
                                        continue
                                else:
                                    subtype = item["subtype"]
                                if anariObjectName == "geometry" and parsed_json["sceneParameters"]["geometrySubtype"] != None and subtype != parsed_json["sceneParameters"]["geometrySubtype"]:
                                    # If geometrySubtype is not the same as subtype ignore the generic anari object
                                    continue
                                ctsType = ""
                                if "ctsType" in item:
                                    ctsType = item["ctsType"]
                                sceneGenerator.createAnariObject(anariObjectName, subtype, ctsType)
                                for [paramName, paramValue] in item.items():
                                    if paramName == "subtype" or paramName == "ctsType":
                                        continue
                                    if isinstance(paramValue, str) and paramValue.startswith("ref_"):
                                        stringArray = paramValue.split('_')
                                        references.append([anariObjectName, idx, paramName, stringArray[1], int(stringArray[2])])
                                    elif isinstance(paramValue, list) and all(isinstance(elem, str) and elem.startswith("ref_") for elem in paramValue):
                                        refList = []
                                        stringArray = []
                                        for elem in paramValue:
                                            stringArray = elem.split('_')
                                            refList.append(int(stringArray[2]))
                                        references.append([anariObjectName, idx, paramName, stringArray[1], refList])
                                    elif paramValue is None:
                                        sceneGenerator.unsetGenericParameter(paramName)
                                    elif isinstance(paramValue, dict):
                                        for [type, typedValue] in paramValue.items():
                                            passByType(paramName, type, typedValue, sceneGenerator)
                                            break
                                    else:
                                        sceneGenerator.setGenericParameter(paramName, paramValue)
                                sceneGenerator.releaseAnariObject()
                    elif key == "gltf":
                        gltf = ctsGLTF.loadGLB(Path(value)) if value.endswith(".glb") else ctsGLTF.loadGLTF(Path(value))
                        sceneGenerator.loadGLTF(json.dumps(gltf.json), gltf.buffers, gltf.images)
                    else:
                        sceneGenerator.setParameter(key, value)
                
                for reference in references:
                    sceneGenerator.setReferenceParameter(*reference)
            except Exception as e:
                print(e)
                continue

        if "permutations" in parsed_json or ("variants" in parsed_json and not only_permutations):
            # gather permutations/variants from test scene json
            variant_keys = []
            keys = []
            lists = []
            permutations = []
            if "permutations" in parsed_json:
                keys.extend(list(parsed_json["permutations"].keys()))
                lists.extend(list(parsed_json["permutations"].values()))
            if "variants" in parsed_json and not only_permutations:
                variant_keys = list(parsed_json["variants"].keys())
                variant_keys = ["var_" + item for item in variant_keys]
                keys.extend(variant_keys)
                lists.extend(list(parsed_json["variants"].values()))
            if "simplified_permutations" in parsed_json and parsed_json["simplified_permutations"]:
                for permutationList in lists:
                    for item in permutationList:
                        permutation = []
                        for permutationList2 in lists:
                            if permutationList2 is not permutationList:                                                      
                                permutation.append(permutationList2[0])
                            else:
                                permutation.append(item)
                        if permutation not in permutations:
                            permutations.append(permutation)
            else:
                permutations = itertools.product(*lists)
            # loop over all permutations based on permutations and variants in the test scene
            for permutation in permutations:
                # prepare naming scheme for saving the results
                permutationString = ""
                variantString = ""
                hasError = False
                for i in range(len(permutation)) :
                    key = None
                    if keys[i] in variant_keys:
                        key = (keys[i])[4:]
                        variantString += f'{"_{}".format(permutation[i])}'
                    else:
                        key = keys[i]
                        permutationString += f'{"_{}".format(permutation[i])}'

                    if use_generator:
                        # set up scene generator with the permutated data for this rendering
                        try:
                            if key.startswith("/anari_objects"):
                                pointer = key.split('/')
                                if (isinstance(permutation[i], str) and permutation[i].startswith("ref_")):
                                    ref = permutation[i].split('_')
                                    sceneGenerator.setReferenceParameter(pointer[2], int(pointer[3]), pointer[4], ref[1], int(ref[2]))
                                elif isinstance(permutation[i], list) and all(isinstance(elem, str) and elem.startswith("ref_") for elem in permutation[i]):
                                        refList = []
                                        stringArray = []
                                        for elem in permutation[i]:
                                            stringArray = elem.split('_')
                                            refList.append(int(stringArray[2]))
                                        sceneGenerator.setReferenceParameter(pointer[2], int(pointer[3]), pointer[4], stringArray[1], refList)
                                else:
                                    sceneGenerator.setCurrentObject(pointer[2], int(pointer[3]))
                                    if permutation[i] is None:
                                        sceneGenerator.unsetGenericParameter(pointer[4])
                                    elif isinstance(permutation[i], dict):
                                        for [type, typedValue] in permutation[i].items():
                                            passByType(pointer[4], type, typedValue, sceneGenerator)
                                            break
                                    elif len(pointer) > 5:
                                        passByType(pointer[4], pointer[5], permutation[i], sceneGenerator)
                                    else:
                                        sceneGenerator.setGenericParameter(pointer[4], permutation[i])
                            else:
                                sceneGenerator.setParameter(key, permutation[i])
                        except Exception as e:
                            print(e)
                            hasError = True
                            continue
                if hasError:
                    continue
                if use_generator:
                    # set up scene for rendering
                    try:
                        sceneGenerator.resetSceneObjects()
                        sceneGenerator.commit()
                    except Exception as e:
                        print(e)
                        continue
                permutationString = simplifyFileName(permutationString)
                variantString = simplifyFileName(variantString)
                # call function for each permutated/variant test scene and collect return values per test scene permutation/variant
                result[test_name + permutationString + variantString] = (func(parsed_json, sceneGenerator, anari_renderer, json_file_path, test_name, permutationString[1:], variantString[1:], *args))
        else:
            if use_generator:
                # prepare regular (non permutated/variant) scene for rendering
                try:
                    sceneGenerator.commit()
                except Exception as e:
                    print(e)
                    continue
            # call function for each test scene and collect return values per test scene
            result[test_name] = (func(parsed_json, sceneGenerator, anari_renderer, json_file_path, test_name,"", "", *args))
    return result

# apply the render_scene() function to all test scenes available
def render_scenes(anari_library, anari_device = None, anari_renderer = "default", test_scenes = "test_scenes", check_features = True, output = "."):
    apply_to_scenes(render_scene, anari_library, anari_device, anari_renderer, test_scenes, False, check_features, True, output)

# compare existing candidate and reference images and write the results into a pdf report
# this report will only contain image comparisons and no further data like e.g. queried features
def compare_images(test_scenes = "test_scenes", candidates_path = "test_scenes", output = ".", verbosity=0, comparison_methods = ["ssim"], thresholds = None, custom_compare_function = None):
    # gather existing candidate and reference images
    ref_images = globImages(test_scenes, reference_prefix)
    candidate_images = globImages(candidates_path, exclude_prefix=reference_prefix)
    # evaluate images per test scene (also generates diff and threshold images)
    evaluations = apply_to_scenes(evaluate_scene, "", None, "default", test_scenes, False, True , False, output, ref_images, candidate_images, comparison_methods, thresholds, custom_compare_function)
    if output != None:
        # write all images to filesystem to later incorporate in report
        print("\n***Create Report***\n")
        evaluations = write_images(evaluations, output)
        merged_evaluations = {}
        for evaluation in evaluations.values():
            merged_evaluations = recursive_update(merged_evaluations, evaluation)
        # write out pdf report containing images and evaluation data
        write_report(merged_evaluations, output, True, verbosity)
        print("***Done***")

# compare candidate bounding box against reference bounding box using a tolerance value
# return error message to list in a report if unsuccessful
def check_bounding_boxes(ref, candidate, tolerance):
    if ref == "Infinity":
        if math.isinf(candidate[0][0]):
            return ""
        else:
            return "Reference bounding box is infinite, candidate is not\n"
    if len(ref) != 2 or len(ref[0]) != 3 or len(ref[1]) != 3:
        return terminalColors.warning("Reference bounding box has wrong format\n")
    if (len(candidate) != 2 or len(candidate[0]) != 3 or len(candidate[1]) != 3):
        return terminalColors.error("Candidate bounding box has wrong format\n")
    axis = 'X'
    output = ""
    for i in range(3):
        ref_values = [ref[0][i], ref[1][i]]
        ref_values.sort()
        ref_distance = ref_values[1] - ref_values[0]
        candidate_values = [candidate[0][i], candidate[1][i]]
        candidate_values.sort()
        for j in range(2):
            diff = abs(ref_values[j] - candidate_values[j])
            if diff > ref_distance * tolerance:
                id = "MIN" if j == 0 else "MAX"
                output += f'{id} {chr(ord(axis) + i)} mismatch: Is {candidate_values[j]}. Should be {ref_values[j]} ± {ref_distance*tolerance}\n'
    return output

# compare metadata from test scene json files to reference meta data after rendering took place
def check_object_properties_helper(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString):
    output = ""
    tolerance = parsed_json["bounds_tolerance"]
    try:
        bounds = sceneGenerator.getBounds()
    except Exception as e:
        print(e)
        return terminalColors.error("Error while retrieving bounds"), False
    if "metaData" in parsed_json:
        metaData = parsed_json["metaData"]
        if permutationString != "" and permutationString in metaData:
            metaData = metaData[permutationString]
        if "bounds" not in metaData:
            message = terminalColors.warning(f'Bounds missing in reference')
            output += message
            return output, False
        ref_bounds = metaData["bounds"]
        if "world" not in ref_bounds:
            message = terminalColors.warning(f'Bounds missing in reference')
            output += message
            return output, False
        check_output = check_bounding_boxes(ref_bounds["world"], bounds[0][0], tolerance)
        if check_output != "":
            message = terminalColors.error(f'Worlds bounds do not match!\n' + check_output)
            output += message
        if "instances" in ref_bounds:
            for i in range(len(ref_bounds["instances"])):
                check_output = check_bounding_boxes(ref_bounds["instances"][i], bounds[1][i], tolerance)
                if check_output != "":
                    message = terminalColors.error(f'Instance {i} bounds do not match!\n' + check_output)
                    output += message
        if "groups" in ref_bounds:
            for i in range(len(ref_bounds["groups"])):
                check_output = check_bounding_boxes(ref_bounds["groups"][i], bounds[2][i], tolerance)
                if check_output != "":
                    message = terminalColors.error(f'Group {i} bounds do not match!\n'+ check_output)
                    output += message
    else:
        message = terminalColors.warning(f'MetaData missing in reference')
        output += message
    success = False
    if output == "":
        success = True
        output = terminalColors.success(f'All bounds correct')
    return output, success

def check_object_properties(anari_library, anari_device = None, test_scenes = "test_scenes"):
    return apply_to_scenes(check_object_properties_helper, anari_library, anari_device, "default", test_scenes)

def query_metadata(anari_library, type = None, subtype = None, skipParameters = False, info = False):
    try:
        return anariCTSBackend.query_metadata(anari_library, type, subtype, skipParameters, info, anari_logger)
    except Exception as e:
        return str(e)

# applied to each test scene from create_report() to render the scene or to gather all rendered images and evaluate them
def create_report_for_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output, methods, thresholds, custom_compare_function, feature_list):
    # construct report name for this test scene
    name = f'{scene_location.stem}'
    if permutationString != "":
        name += f'_{permutationString}'
    if variantString != "":
        name += f'_{variantString}'
    # setup report data structure for this test scene
    report = {}
    report[test_name] = {}
    report[test_name][name] = {}

    # gather descriptions for report
    if "description" in parsed_json:
        report[test_name]["description"] = parsed_json["description"]

    if sceneGenerator != None:
        # render test scenes and gather frame duration and property check results for the report
        frame_duration = render_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output)
        property_check = check_object_properties_helper(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString)
        report[test_name][name]["frameDuration"] = frame_duration
        report[test_name][name]["property_check"] = property_check
    else:
        # check wether all features required for this test scene are available on the device
        not_available_features = []
        if "requiredFeatures" in parsed_json:
            for feature in parsed_json["requiredFeatures"]:
                if not check_feature(feature_list, feature):
                    not_available_features.append(feature)

        if len(not_available_features) == 0:
            # gather candidate and reference images rendered previously and add their evaulation to the report
            ref_images = globImages(scene_location.parent, reference_prefix)
            candidate_images = globImages(output / Path(test_name).parent, exclude_prefix=reference_prefix)
            report = evaluate_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output, ref_images, candidate_images, methods, thresholds, custom_compare_function)
        else:
            # list unsupported features that were required for this test scene in the report, no image evaluation possible
            report[test_name]["not_supported"] = not_available_features
            report[test_name]["requiredFeatures"] = parsed_json["requiredFeatures"]
    return report

# queries metadata and features, renders test scenes, compares reference and candidate images and writes all results into a pdf report
def create_report(library, device = None, renderer = "default", test_scenes = "test_scenes", output = ".", verbosity = 0, check_features = True, comparison_methods = ["ssim"], thresholds = None, custom_compare_function = None):
    # query metadata and features from the library / device
    merged_evaluations = {}
    merged_evaluations["anariInfo"] = query_metadata(library, device)
    merged_evaluations["features"] = query_features(library, device)
    merged_evaluations["renderer"] = renderer
    merged_evaluations["library"] = library
    try:
        merged_evaluations["device"] = device if device != None else anariCTSBackend.getDefaultDeviceName(library, anari_logger)
    except Exception as e:
        print(e)
        return
    print("***Create renderings***\n")
    # render test scenes
    result1 = apply_to_scenes(create_report_for_scene, library, device, renderer, test_scenes, False, check_features, True, output, comparison_methods, thresholds, custom_compare_function, merged_evaluations["features"])
    if not result1:
        print("Report could not be created")
        return
    print("\n***Compare renderings***\n")
    # evaluate rendered images
    result2 = apply_to_scenes(create_report_for_scene, library, device, renderer, test_scenes, False, check_features, False, output, comparison_methods, thresholds, custom_compare_function, merged_evaluations["features"])
    # write all images to filesystem to later incorporate in report
    result2 = write_images(result2, output)
    # combine rendering results and image evaluation results with previously queried data
    for evaluation in result1.values():
        merged_evaluations = recursive_update(merged_evaluations, evaluation)
    for evaluation in result2.values():
        merged_evaluations = recursive_update(merged_evaluations, evaluation)
    print("\n***Create Report***\n")
    # write out extensive pdf report (depending on verbosity level)
    write_report(merged_evaluations, output, check_features, verbosity)
    print("***Done***")

# collect information per test
def query_scene_info(parsed_json):
    info = {}
    if "description" in parsed_json:
        info["description"] = parsed_json["description"]
    else:
        info["description"] = "No description available"
    if "requiredFeatures" in parsed_json:
        info["required_features"] = parsed_json["requiredFeatures"]
    else:
        info["required_features"] = ["No required features"]
    return info

# print test scene information to console
def print_scene_info(test_name, info):
    print("")
    # name of the test
    print(test_name)
    # description
    print("   description: " + info["description"])
    # required features
    required_features = "   required Features: "
    for feature in info["required_features"]:
        required_features += feature + ", "
    required_features = required_features.removesuffix(", ")
    print(required_features)


# parses json data from test scene files and returns tuple of information per test
def parse_scenes_info(test_scenes):
    result = {}
    # gather available test scenes
    collected_scenes = resolve_scenes(test_scenes)
    if collected_scenes == []:
        print("No scenes selected")
        return result
    
    # loop over test scenes
    for json_file_path in collected_scenes:
        # parse test scene json files
        test_name = json_file_path.name
        scene_location_parts = json_file_path.parts
        if "test_scenes" in scene_location_parts:
            test_scenes_index = scene_location_parts[::-1].index("test_scenes")
            test_name = str(Path(*(scene_location_parts[len(scene_location_parts) - test_scenes_index - 1:])).with_suffix(""))

        parsed_json = {}
        with open(json_file_path, 'r') as f:
            parsed_json = json.load(f)
            
        result[test_name] = query_scene_info(parsed_json)
    
    return result

# gathers information about each test scene, then prints it to the console
def query_scenes_info(test_scenes):
    scenes_info = parse_scenes_info(test_scenes)
    for info_tuple in scenes_info.items():
        print_scene_info(info_tuple[0], info_tuple[1])

if __name__ == "__main__":
    # setup parent argument parsers
    parser = argparse.ArgumentParser(description='ANARI CTS toolkit', formatter_class=argparse.RawTextHelpFormatter)
    subparsers = parser.add_subparsers(dest="command", title='Commands', metavar="")

    libraryParser = argparse.ArgumentParser(add_help=False)
    libraryParser.add_argument('library', help='ANARI library to load')
    libraryParser.add_argument('--log_dir', default=None, type=Path, help='Directory in which ANARI.log file is saved. Defaults to working directory')

    ignoreFeatureParser = argparse.ArgumentParser(add_help=False)
    ignoreFeatureParser.add_argument('--ignore_features', action='store_true', help='Run tests even if feature is not supported')

    deviceParser = argparse.ArgumentParser(add_help=False, parents=[libraryParser])
    deviceParser.add_argument('-d', '--device', default=None, help='ANARI device on which to perform the test')

    sceneParser = argparse.ArgumentParser(add_help=False, parents=[deviceParser])
    sceneParser.add_argument('-r', '--renderer', default="default", help="Renderer used to render the images")
    sceneParser.add_argument('-t', '--test_scenes', default="test_scenes", help="Folder with test scenes to test. Specify subfolder to test subsets. Also accepts glob patterns")

    evaluationMethodParser = argparse.ArgumentParser(add_help=False)
    evaluationMethodParser.add_argument('--comparison_methods', default=["ssim"], nargs='+', choices=["ssim", "psnr"], help="Specify all comparison methods to test against")
    evaluationMethodParser.add_argument('--thresholds', default=None, nargs='+', help="Specify custom thresholds for each comparison method")
    evaluationMethodParser.add_argument('-v', '--verbose_error', action='store_true', help="Include verbose infos of failed tests in report")
    evaluationMethodParser.add_argument('-vv', '--verbose_all', action='store_true', help="Include verbose infos of all tests in report")

    # command: render_scenes
    renderScenesParser = subparsers.add_parser('render_scenes', description='Renders an image to disk for each test scene', parents=[sceneParser,ignoreFeatureParser])
    renderScenesParser.add_argument('-o', '--output', default=".", help="Output path")

    # command: compare_images
    evaluateScenesParser = subparsers.add_parser('compare_images', description='Evaluates candidate renderings against reference renderings', parents=[evaluationMethodParser])
    evaluateScenesParser.add_argument('-t', '--test_scenes', default="test_scenes", help="Folder with test scenes which include the reference images")
    evaluateScenesParser.add_argument('--candidates', default="test_scenes", help="Path to folder containing the candidate images")
    evaluateScenesParser.add_argument('-o', '--output', default=".", help="Output path")

    # command: query_features
    checkExtensionsParser = subparsers.add_parser('query_features', parents=[deviceParser], description="Check which features are supported by the device")

    # command: query_metadata
    queryMetadataParser = subparsers.add_parser('query_metadata', parents=[libraryParser], description="Show metadata related to the specified library")
    queryMetadataParser.add_argument('--type', default=None, help='Only show parameters for objects of a type')
    queryMetadataParser.add_argument('--subtype', default=None, help='Only show parameters for objects of a subtype')
    queryMetadataParser.add_argument('--skipParameters', action='store_true', help='Skip parameter listing')
    queryMetadataParser.add_argument('--info', action='store_true', help='Show detailed information for each parameter')

    # command: check_object_properties
    checkObjectPropertiesParser = subparsers.add_parser('check_object_properties', parents=[deviceParser], description="Check if all properties are similar to the reference properties")
    checkObjectPropertiesParser.add_argument('-t', '--test_scenes', default="test_scenes", help="Folder with test scenes to test. Specify subfolder to test subsets. Also accepts glob patterns")

    # command: create_report
    create_reportParser = subparsers.add_parser('create_report', parents=[sceneParser, evaluationMethodParser, ignoreFeatureParser], description="Runs all tests and creates a pdf report")
    create_reportParser.add_argument('-o', '--output', default=".", help="Output path")

    # command: query_scenes_info
    queryInfoParser = subparsers.add_parser('query_scenes_info', description="Lists information about the given test scene(s)")
    queryInfoParser.add_argument('-t', '--test_scenes', default="test_scenes", help="Folder with test scenes OR a test scene file to gather information from. Also accepts glob patterns")
    queryInfoParser.add_argument('--log_dir', default=None, type=Path, help='Directory in which ANARI.log file is saved. Defaults to working directory')

    command_text = ""
    for subparser in subparsers.choices :
        subparsertext = subparser
        if subparsers.choices[subparser].description:
            subparsertext = subparsertext.ljust(30) + subparsers.choices[subparser].description
        subparsertext += "\n"
        command_text += subparsertext
    subparsers.help = command_text

    # parse command and call corresponding functionality
    args = parser.parse_args()

    if hasattr(args, 'log_dir') and args.log_dir is not None:
        log_file_path = args.log_dir / log_file_name
        os.makedirs(args.log_dir, exist_ok=True)
    else:
        log_env = os.environ.get('ANARI_CTS_LOG')
        if log_env is not None:
            log_file_path = Path(log_env) / log_file_name
            os.makedirs(Path(log_env), exist_ok=True)

    verboseLevel = 2 if "verbose_all" in args and args.verbose_all else 1 if "verbose_error" in args and args.verbose_error else 0

    if args.command == "render_scenes":
        render_scenes(args.library, args.device, args.renderer, args.test_scenes, not args.ignore_features, args.output)
    elif args.command == "compare_images":
        compare_images(args.test_scenes, args.candidates, args.output, verboseLevel, args.comparison_methods, args.thresholds)
    elif args.command == "query_features":
        result = query_features(args.library, args.device)
        print(tabulate(result))
    elif args.command == "query_metadata":
        print(query_metadata(args.library, args.type, args.subtype, args.skipParameters, args.info))
    elif args.command == "check_object_properties":
        result = check_object_properties(args.library, args.device, args.test_scenes)
        for key, value in result.items():
            if (isinstance(value, str)):
                continue
            else:
                print(f'{key}: {value[0]}')
    elif args.command == "create_report":
        create_report(args.library, args.device, args.renderer, args.test_scenes, args.output, verboseLevel, not args.ignore_features, args.comparison_methods, args.thresholds)
    elif args.command == "query_scenes_info":
        query_scenes_info(args.test_scenes)
