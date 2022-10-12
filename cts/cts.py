#!python3
import ctsBackend
import argparse
import threading
import datetime
from pathlib import Path
from tabulate import tabulate
from PIL import Image
import json

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

def render_scenes(anari_library, anari_device = "default", anari_renderer = "default", test_scenes = "test_scenes", output = "."):
    collected_scenes = resolve_scenes(test_scenes)
    if collected_scenes == []:
        print("No scenes selected")
        return

    HEIGHT = 1024
    WIDTH = 1024

    print(collected_scenes)

    sceneGenerator = ctsBackend.SceneGenerator(anari_library, anari_device, anari_logger)

    print('Initialized generator')

    for json_file in collected_scenes:
        with open(json_file, 'r') as f:
            parsed_json = json.load(f)
            print(f'Rendering: {str(json_file)}')
            for [key, value] in parsed_json.items():
                if isinstance(value, dict):
                    if key == "permutations":
                        #TODO
                        print("Test")
                        #ctsBackend.setPermutations(value)
                else:
                    sceneGenerator.setParameter(key, value)

            sceneGenerator.setParameter("primitiveMode", "soup")
            sceneGenerator.commit()
            image_data_list = sceneGenerator.renderScene(anari_renderer)
            stem = json_file.stem
            channels = ["color", "depth"]
            image_out = Image.new("RGBA", (HEIGHT, WIDTH))
            image_out.putdata(image_data_list[0])
            image_out.save(json_file.with_suffix('.png').with_stem(f'{stem}_{channels[0]}'))

            image_out = Image.new("RGBA", (HEIGHT, WIDTH))
            image_out.putdata(image_data_list[1])
            image_out.save(json_file.with_suffix('.png').with_stem(f'{stem}_{channels[1]}'))



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