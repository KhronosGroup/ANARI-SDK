#!python3
import ctsBackend
import argparse
from pathlib import Path




def render_scenes(anari_library, anari_device = None, anari_renderer = "default", test_scenes = "all", output = "."):
    ctsBackend.render_scenes(anari_library, anari_device, anari_renderer, test_scenes, output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ANARI CTS toolkit')
    subparsers = parser.add_subparsers(dest="command")

    renderScenesParser = subparsers.add_parser('render_scenes')
    renderScenesParser.add_argument('library', required=True)
    renderScenesParser.add_argument('--device', default=None)
    renderScenesParser.add_argument('--renderer', default="default")
    renderScenesParser.add_argument('--test_scenes', default="all")
    renderScenesParser.add_argument('--output', default=".")

    args = parser.parse_args()

    if args.command == "render_scenes":
        render_scenes(args.library, args.device, args.renderer, args.test_scenes, args.output)