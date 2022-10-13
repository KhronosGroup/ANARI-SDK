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

def anari_logger(message):
    with logger_mutex:
        with open("ANARI.log", 'a') as file:
            file.write(f'{str(datetime.datetime.now())}: {message}\n')
    #print(message)

def check_core_extensions(anari_library, anari_device = None):
    return ctsBackend.check_core_extensions(anari_library, anari_device, anari_logger)

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

def render_scene(parsed_json, sceneGenerator, anari_renderer, file_name):
    for [key, value] in parsed_json.items():
        if not isinstance(value, dict):
            sceneGenerator.setParameter(key, value)
    sceneGenerator.commit()
    image_data_list = sceneGenerator.renderScene(anari_renderer)
    stem = file_name.stem
    channels = ["color", "depth"]
    image_out = Image.new("RGBA", (parsed_json["image_height"], parsed_json["image_width"]))
    image_out.putdata(image_data_list[0])
    outName = file_name.with_suffix('.png').with_stem(f'{stem}_{channels[0]}')
    print(f'Rendering to {outName}')
    image_out.save(outName)

    image_out = Image.new("RGBA", (parsed_json["image_height"], parsed_json["image_width"]))
    image_out.putdata(image_data_list[1])
    outName = file_name.with_suffix('.png').with_stem(f'{stem}_{channels[1]}')
    print(f'Rendering to {outName}')
    image_out.save(outName)

def render_scenes(anari_library, anari_device = "default", anari_renderer = "default", test_scenes = "test_scenes", output = "."):
    collected_scenes = resolve_scenes(test_scenes)
    if collected_scenes == []:
        print("No scenes selected")
        return

    print(collected_scenes)

    sceneGenerator = ctsBackend.SceneGenerator(anari_library, anari_device, anari_logger)

    print('Initialized generator')

    for json_file in collected_scenes:
        with open(json_file, 'r') as f, open('default_test_scene.json', 'r') as defaultTestScene:
            parsed_json = json.load(defaultTestScene)
            parsed_json.update(json.load(f))

            if "permutations" in parsed_json:
                keys = list(parsed_json["permutations"].keys())
                lists = list(parsed_json["permutations"].values())
                permutations = itertools.product(*lists)
                for permutation in permutations:
                    for i in range(len(permutation)) :
                        sceneGenerator.setParameter(keys[i], permutation[i])
                    file_name = json_file.with_stem(f'{json_file.stem}{len(permutation)*"_{}".format(*permutation)}')
                    render_scene(parsed_json, sceneGenerator, anari_renderer, file_name)
            else:
                render_scene(parsed_json, sceneGenerator, anari_renderer, json_file)




def query_metadata(anari_library, type = None, subtype = None, skipParameters = False, info = False):
    ctsBackend.query_metadata(anari_library, type, subtype, skipParameters, info, anari_logger)



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ANARI CTS toolkit')
    subparsers = parser.add_subparsers(dest="command", title='Commands', metavar=None)

    libraryParser = argparse.ArgumentParser(add_help=False)
    libraryParser.add_argument('library', help='ANARI library to load')
    deviceParser = argparse.ArgumentParser(add_help=False, parents=[libraryParser])
    deviceParser.add_argument('--device', default="default", help='ANARI device on which to perform the test')

    renderScenesParser = subparsers.add_parser('render_scenes', description='Renders an image to disk for each test scene', parents=[deviceParser])
    renderScenesParser.add_argument('--renderer', default="default")
    renderScenesParser.add_argument('--test_scenes', default="test_scenes")
    renderScenesParser.add_argument('--output', default=".")

    checkExtensionsParser = subparsers.add_parser('check_core_extensions', parents=[deviceParser])

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
    elif args.command == "check_core_extensions":
        result = check_core_extensions(args.library, args.device)
        print(tabulate(result))
    elif args.command == "query_metadata":
        query_metadata(args.library, args.type, args.subtype, args.skipParameters, args.info)