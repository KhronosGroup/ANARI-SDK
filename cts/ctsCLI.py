import argparse
import cts

def render_scenes(args):
    cts.render_scenes(args.library, args.device, args.renderer, args.test_scenes, args.output)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ANARI CTS toolkit')
    subparsers = parser.add_subparsers()

    renderScenesParser = subparsers.add_parser('render_scenes')
    renderScenesParser.add_argument('library', required=True)
    renderScenesParser.add_argument('--device', default=None)
    renderScenesParser.add_argument('--renderer', default="default")
    renderScenesParser.add_argument('--test_scenes', default="all")
    renderScenesParser.add_argument('--output', default=".")

    args = parser.parse_args()

    args.func(args)
