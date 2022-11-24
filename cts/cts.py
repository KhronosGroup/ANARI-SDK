#!python3
import ctsBackend
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
import math

logger_mutex = threading.Lock()

reference_prefix = "ref_"

def check_feature(feature_list, check_feature):
    for [feature, is_available] in feature_list:
        if feature == check_feature:
            return is_available
    return False

def get_channels(parsed_json):
    channels = []
    if "sceneParameters" in parsed_json:
        scene_parameters = parsed_json["sceneParameters"]
        if "frame_color_type" in scene_parameters and scene_parameters["frame_color_type"] != "":
            channels.append("color")
        if "frame_depth_type" in scene_parameters and scene_parameters["frame_depth_type"] != "":
            channels.append("depth")
    return channels

def anari_logger(message):
    with logger_mutex:
        with open("ANARI.log", 'a') as file:
            file.write(f'{str(datetime.datetime.now())}: {message}\n')

def query_features(anari_library, anari_device = None):
    try: 
        return ctsBackend.query_features(anari_library, anari_device, anari_logger)
    except Exception as e:
        print(e)
        return []

def recursive_update(d, merge_dict):
    if not isinstance(d, dict) or not isinstance(merge_dict, dict):
        return d
    for key, value in d.items():
        if isinstance(value, dict) and key in merge_dict:
            merge_dict[key] = recursive_update(value, merge_dict[key])
    d.update(merge_dict)        
    return d

def globImages(directory, prefix = "", exclude_prefix = ""):
    path_list = glob.glob(f'{str(directory)}/**/{prefix}*.png', recursive = True)
    if exclude_prefix != "":
        path_list = [path for path in path_list if not Path(path).name.startswith(exclude_prefix)]
    return path_list


def getFileFromList(list, filename):
    for path in list:
        if Path(path).stem == filename:
            return path
    return ""

# writes all reference, candidate, diff and threshold images to disk and returns
def write_images(evaluations, output):
    output_path = Path(output) / "evaluation"
    for evaluationKey, evaluation in evaluations.items():
        for stem, test in evaluation.items():
            for name, value in test.items():
                if isinstance(value, dict):
                    for channel, channelValue in value.items():
                        if isinstance(channelValue, dict):
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

def write_report(results, output, verbosity = 0):
    output_path = Path(output) / "evaluation"
    title = f'CTS - Report' if "renderer" not in results else f'CTS - Library: {results["library"]} Device: {results["device"]} Renderer: {results["renderer"]}'
    ctsReport.generate_report_document(results, output_path, title, verbosity)

def evaluate_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output = ".", ref_files = [], candidate_files = [], methods = ["ssim"], thresholds = None, custom_compare_function = None):
    results = {}
    stem = scene_location.stem
    results[test_name] = {}
    if "requiredFeatures" in parsed_json:
        results[test_name]["requiredFeatures"] = parsed_json["requiredFeatures"]
    channels = get_channels(parsed_json)
    
    if permutationString != "":
        permutationString = f'_{permutationString}'
        
    if variantString != "":
        variantString = f'_{variantString}'

    name = f'{stem}{permutationString}{variantString}'
    results[test_name][name] = {}

    for channel in channels:
        reference_file = f'{reference_prefix}{stem}{permutationString}_{channel}'
        candidate_file = f'{stem}{permutationString}{variantString}_{channel}'

        ref_path = getFileFromList(ref_files, reference_file)
        candidate_path = getFileFromList(candidate_files, candidate_file)
        if ref_path == "":
            print('No reference image for filepath {} could be found.'.format(reference_file))
            continue
        
        if candidate_path == "":
            print('No candidate images for filepath {} could be found.'.format(candidate_file))
            continue
        
        if channel == "depth":
            methods = ["psnr"]
            channelThresholds = [20.0]
            custom_compare_function = None
        else:
            channelThresholds = thresholds
        eval_result = ctsUtility.evaluate_scene(ref_path, candidate_path, methods, channelThresholds, custom_compare_function)
        print(f'\n{test_name} {name} {channel}:')
        for key in eval_result["metrics"]:
            hasPassed = "\033[92mPassed\033[0m" if eval_result["passed"][key] else "\033[91mFailed\033[0m"
            print(f'{key}: {hasPassed} {eval_result["metrics"][key]} Threshold: {eval_result["thresholds"][key]}')
        results[str(test_name)][name][channel] = eval_result
    return results

def resolve_scenes(test_scenes):
    collected_scenes = []
    if isinstance(test_scenes, list):
        for test_scene in test_scenes:            
            path = Path(test_scene)
            if path.exists():
                collected_scenes.append(path)
            else:
                print(f'Path does not exist: {str(path)}')
    else:
        path = Path(Path(__file__).parent / test_scenes)
        if not path.is_dir():
            print("No valid category")
            return []
        collected_scenes = list(path.rglob("*.json"))
    return collected_scenes

def render_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output = ".", prefix = ""):
    world_bounds = []
    if "metaData" in [parsed_json]:
        metaData = parsed_json["metaData"]
        if permutationString != "":
            metaData = metaData[permutationString]
        if "bounds" in metaData and "world" in metaData["bounds"]:
            world_bounds = metaData["bounds"]["world"]

    if world_bounds == []:
        world_bounds = sceneGenerator.getBounds()[0][0]

    bounds_distance = math.dist(world_bounds[0], world_bounds[1])
    image_data_list = sceneGenerator.renderScene(anari_renderer, bounds_distance)

    frame_duration = sceneGenerator.getFrameDuration()
    print(f'Frame duration: {frame_duration}')

    output_path = Path(output)
    
    if permutationString != "":
        permutationString = f'_{permutationString}'
    
    if variantString != "":
        permutationString += f'_{variantString}'


    file_name = output_path / Path(test_name)

    stem = scene_location.stem
    channels = get_channels(parsed_json)

    file_name.parent.mkdir(exist_ok=True, parents=True)

    if "color" in channels:
        image_out = Image.new("RGBA", (parsed_json["sceneParameters"]["image_height"], parsed_json["sceneParameters"]["image_width"]))
        image_out.putdata(image_data_list[0])
        outName = file_name.with_suffix('.png').with_stem(f'{prefix}{stem}{permutationString}_color')
        print(f'Rendering to {outName.resolve()}')
        image_out.save(outName)

    if "depth" in channels:
        image_out = Image.new("RGBA", (parsed_json["sceneParameters"]["image_height"], parsed_json["sceneParameters"]["image_width"]))
        image_out.putdata(image_data_list[1])
        outName = file_name.with_suffix('.png').with_stem(f'{prefix}{stem}{permutationString}_depth')
        print(f'Rendering to {outName.resolve()}')
        image_out.save(outName)
    print("")
    return frame_duration

def apply_to_scenes(func, anari_library, anari_device = None, anari_renderer = "default", test_scenes = "test_scenes", only_permutations = False, use_generator = True,  *args):
    result = {}
    collected_scenes = resolve_scenes(test_scenes)
    if collected_scenes == []:
        print("No scenes selected")
        return result

    sceneGenerator = None
    if use_generator:
        try:
            sceneGenerator = ctsBackend.SceneGenerator(anari_library, anari_device, anari_logger)
        except Exception as e:
            print(e)
            return result
        print('Initialized scene generator\n')
        feature_list = query_features(anari_library, anari_device)

    for json_file_path in collected_scenes:

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
            all_features_available = True
            if "requiredFeatures" in parsed_json:
                for feature in parsed_json["requiredFeatures"]:
                    if not check_feature(feature_list, feature):
                        all_features_available = False
                        print("Feature %s is not supported"%feature)
            
            if not all_features_available:
                print("Scene %s is not supported"%json_file_path)
                result[test_name] = "Features not supported"
                continue
        
            sceneGenerator.resetAllParameters()
            for [key, value] in parsed_json["sceneParameters"].items():
                sceneGenerator.setParameter(key, value)

        if "permutations" in parsed_json or "variants" in parsed_json:
            variant_keys = []
            keys = []
            lists = []
            if "permutations" in parsed_json:
                keys.extend(list(parsed_json["permutations"].keys()))
                lists.extend(list(parsed_json["permutations"].values()))
            if "variants" in parsed_json and not only_permutations:
                variant_keys = list(parsed_json["variants"].keys())
                variant_keys = ["var_" + item for item in variant_keys]
                keys.extend(variant_keys)
                lists.extend(list(parsed_json["variants"].values()))
            permutations = itertools.product(*lists)
            for permutation in permutations:
                permutationString = ""
                variantString = ""
                for i in range(len(permutation)) :
                    key = None
                    if keys[i] in variant_keys:
                        key = (keys[i])[4:]
                        variantString += f'{"_{}".format(permutation[i])}'
                    else:
                        key = keys[i]
                        permutationString += f'{"_{}".format(permutation[i])}'
                    
                    if use_generator:
                        sceneGenerator.setParameter(key, permutation[i])
                
                if use_generator:
                    sceneGenerator.commit()
                result[test_name + permutationString + variantString] = (func(parsed_json, sceneGenerator, anari_renderer, json_file_path, test_name, permutationString[1:], variantString[1:], *args))
        else:
            if use_generator:
                sceneGenerator.commit()
            result[test_name] = (func(parsed_json, sceneGenerator, anari_renderer, json_file_path, test_name,"", "", *args))
    return result

def render_scenes(anari_library, anari_device = None, anari_renderer = "default", test_scenes = "test_scenes", output = "."):
    apply_to_scenes(render_scene, anari_library, anari_device, anari_renderer, test_scenes, False, True, output)
    
def compare_images(test_scenes = "test_scenes", candidates_path = "test_scenes", output = ".", verbosity=0, comparison_methods = ["ssim"], thresholds = None, custom_compare_function = None):
    ref_images = globImages(test_scenes, reference_prefix)
    candidate_images = globImages(candidates_path, exclude_prefix=reference_prefix)
    evaluations = apply_to_scenes(evaluate_scene, "", None, "default", test_scenes, False, False, output, ref_images, candidate_images, comparison_methods, thresholds, custom_compare_function)
    evaluations = write_images(evaluations, output)
    merged_evaluations = {}
    for evaluation in evaluations.values():
        merged_evaluations = recursive_update(merged_evaluations, evaluation)
    write_report(merged_evaluations, output, verbosity)

def check_bounding_boxes(ref, candidate, tolerance):
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

def check_object_properties_helper(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString):
    output = ""
    tolerance = parsed_json["bounds_tolerance"]
    bounds = sceneGenerator.getBounds()
    if "metaData" in parsed_json:
        metaData = parsed_json["metaData"]
        if permutationString != "" and permutationString in metaData:
            metaData = metaData[permutationString]
        if variantString != "":
            permutationString += f'_{variantString}'
        if "bounds" not in metaData:
            message = f'Bounds missing in reference'
            output += message
            return output
        ref_bounds = metaData["bounds"]
        if "world" not in ref_bounds:
            message = f'Bounds missing in reference'
            output += message
            return output
        check_output = check_bounding_boxes(ref_bounds["world"], bounds[0][0], tolerance)
        if check_output != "":
            message = f'Worlds bounds do not match!\n' + check_output
            output += message
        if "instances" in ref_bounds:
            for i in range(len(ref_bounds["instances"])):
                check_output = check_bounding_boxes(ref_bounds["instances"][i], bounds[1][i], tolerance)
                if check_output != "":
                    message = f'Instance {i} bounds do not match!\n' + check_output
                    output += message
        if "groups" in ref_bounds:
            for i in range(len(ref_bounds["groups"])):
                check_output = check_bounding_boxes(ref_bounds["groups"][i], bounds[2][i], tolerance)
                if check_output != "":
                    message = f'Group {i} bounds do not match!\n'+ check_output
                    output += message
    else:
        message = f'MetaData missing in reference'
        output += message
    success = False
    if output == "":
        success = True
        output = f'All bounds correct'
    return output, success

def check_object_properties(anari_library, anari_device = None, test_scenes = "test_scenes"):
    return apply_to_scenes(check_object_properties_helper, anari_library, anari_device, None, test_scenes)

def query_metadata(anari_library, type = None, subtype = None, skipParameters = False, info = False):
    try:
        return ctsBackend.query_metadata(anari_library, type, subtype, skipParameters, info, anari_logger)
    except Exception as e:
        return str(e)

def create_report_for_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output, methods, thresholds, custom_compare_function, feature_list):
    name = f'{scene_location.stem}'
    if permutationString != "":
        name += f'_{permutationString}'
    if variantString != "":
        name += f'_{variantString}'
    report = {}
    report[test_name] = {}
    report[test_name][name] = {}
    if sceneGenerator != None:
        frame_duration = render_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output)
        property_check = check_object_properties_helper(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString)
        report[test_name][name]["frameDuration"] = frame_duration
        report[test_name][name]["property_check"] = property_check
    else:
        all_features_available = True
        if "requiredFeatures" in parsed_json:
            for feature in parsed_json["requiredFeatures"]:
                if not check_feature(feature_list, feature):
                    all_features_available = False

        if all_features_available:
            ref_images = globImages(scene_location.parent, reference_prefix)
            candidate_images = globImages(output / Path(test_name).parent, exclude_prefix=reference_prefix)
            report = evaluate_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, test_name, permutationString, variantString, output, ref_images, candidate_images, methods, thresholds, custom_compare_function)
        else:
            report[test_name]["not_supported"] = True
            report[test_name]["requiredFeatures"] = parsed_json["requiredFeatures"]


    return report

def create_report(library, device = None, renderer = "default", test_scenes = "test_scenes", output = ".", verbosity = 0, comparison_methods = ["ssim"], thresholds = None, custom_compare_function = None):
    merged_evaluations = {}
    merged_evaluations["anariInfo"] = query_metadata(library, device)
    merged_evaluations["features"] = query_features(library, device)
    merged_evaluations["renderer"] = renderer
    merged_evaluations["library"] = library
    try:
        merged_evaluations["device"] = device if device != None else ctsBackend.getDefaultDeviceName(library, anari_logger)
    except Exception as e:
        print(e)
        return
    print("***Create renderings***\n")
    result1 = apply_to_scenes(create_report_for_scene, library, device, renderer, test_scenes, False, True, output, comparison_methods, thresholds, custom_compare_function, merged_evaluations["features"])
    if not result1:
        print("Report could not be created")
        return
    print("\n***Compare renderings***\n")
    result2 = apply_to_scenes(create_report_for_scene, library, device, renderer, test_scenes, False, False, output, comparison_methods, thresholds, custom_compare_function, merged_evaluations["features"])
    result2 = write_images(result2, output)
    for evaluation in result1.values():
        merged_evaluations = recursive_update(merged_evaluations, evaluation)
    for evaluation in result2.values():
        merged_evaluations = recursive_update(merged_evaluations, evaluation)
    print("\n***Create Report***\n")
    write_report(merged_evaluations, output, verbosity)
    print("***Done***")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ANARI CTS toolkit')
    subparsers = parser.add_subparsers(dest="command", title='Commands', metavar=None)

    libraryParser = argparse.ArgumentParser(add_help=False)
    libraryParser.add_argument('library', help='ANARI library to load')

    deviceParser = argparse.ArgumentParser(add_help=False, parents=[libraryParser])
    deviceParser.add_argument('--device', default=None, help='ANARI device on which to perform the test')

    sceneParser = argparse.ArgumentParser(add_help=False, parents=[deviceParser])
    sceneParser.add_argument('--renderer', default="default")
    sceneParser.add_argument('--test_scenes', default="test_scenes")

    renderScenesParser = subparsers.add_parser('render_scenes', description='Renders an image to disk for each test scene', parents=[sceneParser])
    renderScenesParser.add_argument('--output', default=".")

    evaluationMethodParser = argparse.ArgumentParser(add_help=False)
    evaluationMethodParser.add_argument('--comparison_methods', default=["ssim"], nargs='+', choices=["ssim", "psnr"])
    evaluationMethodParser.add_argument('--thresholds', default=None, nargs='+')
    evaluationMethodParser.add_argument('--verbose_error', action='store_true')
    evaluationMethodParser.add_argument('--verbose_all', action='store_true')

    evaluateScenesParser = subparsers.add_parser('compare_images', description='Evaluates candidate renderings against reference renderings', parents=[evaluationMethodParser])
    evaluateScenesParser.add_argument('--test_scenes', default="test_scenes")
    evaluateScenesParser.add_argument('--candidates', default="test_scenes")
    evaluateScenesParser.add_argument('--output', default=".")

    checkExtensionsParser = subparsers.add_parser('query_features', parents=[deviceParser])

    queryMetadataParser = subparsers.add_parser('query_metadata', parents=[libraryParser])
    queryMetadataParser.add_argument('--type', default=None, help='Only show parameters for objects of a type')
    queryMetadataParser.add_argument('--subtype', default=None, help='Only show parameters for objects of a subtype')
    queryMetadataParser.add_argument('--skipParameters', action='store_true', help='Skip parameter listing')
    queryMetadataParser.add_argument('--info', action='store_true', help='Show detailed information for each parameter')

    checkObjectPropertiesParser = subparsers.add_parser('check_object_properties', parents=[deviceParser])
    checkObjectPropertiesParser.add_argument('--test_scenes', default="test_scenes")

    create_reportParser = subparsers.add_parser('create_report', parents=[sceneParser, evaluationMethodParser])
    create_reportParser.add_argument('--output', default=".")

    command_text = ""
    for subparser in subparsers.choices :
        command_text += subparser + "\n  "
    subparsers.metavar = command_text

    args = parser.parse_args()

    verboseLevel = 2 if args.verbose_all else 1 if args.verbose_error else 0

    if args.command == "render_scenes":
        render_scenes(args.library, args.device, args.renderer, args.test_scenes, args.output)
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
            print(f'{key}: {value[0]}')
    elif args.command == "create_report":
        create_report(args.library, args.device, args.renderer, args.test_scenes, args.output, verboseLevel, args.comparison_methods, args.thresholds)
