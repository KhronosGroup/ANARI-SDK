#!python3
import ctsBackend
import argparse
import threading
import datetime
from pathlib import Path
from tabulate import tabulate
from PIL import Image
import json
import itertools

logger_mutex = threading.Lock()

def check_feature(feature_list, check_feature):
    for [feature, is_available] in feature_list:
        if feature == check_feature:
            return is_available
    return False

def anari_logger(message):
    with logger_mutex:
        with open("ANARI.log", 'a') as file:
            file.write(f'{str(datetime.datetime.now())}: {message}\n')
    #print(message)

def query_features(anari_library, anari_device = None, logger = anari_logger):
    try: 
        return ctsBackend.query_features(anari_library, anari_device, logger)
    except Exception as e:
        print(e)
        return []

def resolve_scenes(test_scenes):
    print(test_scenes)
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

def render_scene(parsed_json, sceneGenerator, anari_renderer, scene_location, permutationString, output = ".", prefix = ""):
    image_data_list = sceneGenerator.renderScene(anari_renderer)

    output_path = Path(output)
    
    if permutationString != "":
        permutationString = f'_{permutationString}'

    file_name = "."
    scene_location_parts = scene_location.parts
    if "test_scenes" in scene_location_parts:
        test_scenes_index = scene_location_parts[::-1].index("test_scenes")
        file_name = output_path / Path(*(scene_location_parts[len(scene_location_parts) - test_scenes_index - 1:]))
    else:   
        file_name = output_path.resolve() / scene_location.name

    stem = scene_location.stem
    channels = ["color", "depth"]

    file_name.parent.mkdir(exist_ok=True, parents=True)

    image_out = Image.new("RGBA", (parsed_json["image_height"], parsed_json["image_width"]))
    image_out.putdata(image_data_list[0])
    outName = file_name.with_suffix('.png').with_stem(f'{prefix}{stem}{permutationString}_{channels[0]}')
    print(f'Rendering to {outName.resolve()}')
    image_out.save(outName)

    image_out = Image.new("RGBA", (parsed_json["image_height"], parsed_json["image_width"]))
    image_out.putdata(image_data_list[1])
    outName = file_name.with_suffix('.png').with_stem(f'{prefix}{stem}{permutationString}_{channels[1]}')
    print(f'Rendering to {outName.resolve()}')
    image_out.save(outName)

def apply_to_scenes(func, anari_library, anari_device = None, anari_renderer = "default", test_scenes = "test_scenes", *args):
    collected_scenes = resolve_scenes(test_scenes)
    if collected_scenes == []:
        print("No scenes selected")
        return

    print(collected_scenes)
    sceneGenerator = None
    try:
        sceneGenerator = ctsBackend.SceneGenerator(anari_library, anari_device, anari_logger)
    except Exception as e:
        print(e)
        return
    print('Initialized scene generator')

    feature_list = query_features(anari_library, anari_device, None)

    for json_file_path in collected_scenes:
        parsed_json = {}
        with open(json_file_path, 'r') as f, open('default_test_scene.json', 'r') as defaultTestScene:
            parsed_json = json.load(defaultTestScene)
            parsed_json.update(json.load(f))

        all_features_available = True
        if "requiredFeatures" in parsed_json:
            for feature in parsed_json["requiredFeatures"]:
                if not check_feature(feature_list, feature):
                    all_features_available = False
                    print("Feature %s is not supported"%feature)
        
        if not all_features_available:
            print("Scene %s is not supported"%json_file_path)
            continue

        sceneGenerator.resetAllParameters()
        for [key, value] in parsed_json.items():
            if not isinstance(value, dict) and not key == "requiredFeatures":
                sceneGenerator.setParameter(key, value)

        if "permutations" in parsed_json:
            keys = list(parsed_json["permutations"].keys())
            lists = list(parsed_json["permutations"].values())
            permutations = itertools.product(*lists)
            for permutation in permutations:
                for i in range(len(permutation)) :
                    sceneGenerator.setParameter(keys[i], permutation[i])
                permutationString = f'{len(permutation)*"_{}".format(*permutation)}'
                sceneGenerator.commit()
                func(parsed_json, sceneGenerator, anari_renderer, json_file_path, permutationString[1:], *args)
        else:
            sceneGenerator.commit()
            func(parsed_json, sceneGenerator, anari_renderer, json_file_path, "", *args)

def render_scenes(anari_library, anari_device = None, anari_renderer = "default", test_scenes = "test_scenes", output = ".", prefix = ""):
    apply_to_scenes(render_scene, anari_library, anari_device, anari_renderer, test_scenes, output, prefix)

def query_metadata(anari_library, type = None, subtype = None, skipParameters = False, info = False):
    ctsBackend.query_metadata(anari_library, type, subtype, skipParameters, info, anari_logger)



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ANARI CTS toolkit')
    subparsers = parser.add_subparsers(dest="command", title='Commands', metavar=None)

    libraryParser = argparse.ArgumentParser(add_help=False)
    libraryParser.add_argument('library', help='ANARI library to load')
    deviceParser = argparse.ArgumentParser(add_help=False, parents=[libraryParser])
    deviceParser.add_argument('--device', default=None, help='ANARI device on which to perform the test')

    renderScenesParser = subparsers.add_parser('render_scenes', description='Renders an image to disk for each test scene', parents=[deviceParser])
    renderScenesParser.add_argument('--renderer', default="default")
    renderScenesParser.add_argument('--test_scenes', default="test_scenes")
    renderScenesParser.add_argument('--output', default=".")

    checkExtensionsParser = subparsers.add_parser('query_features', parents=[deviceParser])

    queryMetadataParser = subparsers.add_parser('query_metadata', parents=[libraryParser])
    queryMetadataParser.add_argument('--type', default=None, help='Only show parameters for objects of a type')
    queryMetadataParser.add_argument('--subtype', default=None, help='Only show parameters for objects of a subtype')
    queryMetadataParser.add_argument('--skipParameters', action='store_true', help='Skip parameter listing')
    queryMetadataParser.add_argument('--info', action='store_true', help='Show detailed information for each parameter')

    command_text = ""
    for subparser in subparsers.choices :
        command_text += subparser + "\n  "
    subparsers.metavar = command_text

    args = parser.parse_args()

    if args.command == "render_scenes":
        render_scenes(args.library, args.device, args.renderer, args.test_scenes, args.output)
    elif args.command == "query_features":
        result = query_features(args.library, args.device)
        print(tabulate(result))
    elif args.command == "query_metadata":
        query_metadata(args.library, args.type, args.subtype, args.skipParameters, args.info)