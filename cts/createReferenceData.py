import cts

def writeMetaData(parsed_json, sceneGenerator, scene_location):
    print("TODO")

def createReferenceData(parsed_json, sceneGenerator, anariRenderer, scene_location):
    cts.render_scene(parsed_json, sceneGenerator, anariRenderer, scene_location)
    writeMetaData(parsed_json, sceneGenerator, scene_location)

if __name__ == "__main__":
    cts.apply_to_scenes(createReferenceData, "example", "example")
