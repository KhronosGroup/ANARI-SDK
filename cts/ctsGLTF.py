import json
import base64

class gltTFFiles:
    json = None
    buffers = [] # Sorted by appearance in the JSON file
    images = [] # Sorted by appearance in the JSON file

def loadGLB(file):
    with open(file, "rb") as f:
        data = f.read()
    files = gltTFFiles()
    i = 0
    magic = data[i:i+4]
    i += 4
    if magic != b'glTF':
        raise Exception("Invalid GLB file")
    version = int.from_bytes(data[i:i+4], byteorder='little')
    i += 4
    if version != 2:
        raise Exception("Invalid GLB version")
    length = int.from_bytes(data[i:i+4], byteorder='little')
    i += 4
    jsonLength = int.from_bytes(data[i:i+4], byteorder='little')
    i += 4
    chunkType = data[i:i+4]
    i += 4
    if chunkType != b'JSON':
        raise Exception("Invalid GLB chunk")
    jsonBytes = data[i:i+jsonLength]
    i+= jsonLength
    jsonBytes = jsonBytes.rstrip(bytes.fromhex("20"))
    text = jsonBytes.decode("utf-8")
    files.json = json.loads(text)
    if (i < length):
        bufferLength = int.from_bytes(data[i:i+4], byteorder='little')
        i += 4
        chunkType = data[i:i+4]
        i += 4
        if chunkType != b'BIN\x00':
            raise Exception("Invalid GLB chunk")
        bufferData = data[i:i+bufferLength]
        files.buffers.append(bufferData.rstrip(bytes.fromhex("00")))
    _analyseGLTF(files)
    return files

def loadGLTF(file):
    with open(file, "r") as f:
        text = f.read()
    files = gltTFFiles()
    files.json = json.loads(text)
    _analyseGLTF(files)
    return files

def _analyseGLTF(files):
    if "buffers" in files.json:
        for buffer in files.json["buffers"]:
            if "uri" in buffer:
                files.buffers.append(_loadURI(buffer["uri"]))
    if "images" in files.json:
        for image in files.json["images"]:
            if "uri" in image:
                files.images.append(_loadURI(image["uri"]))


def _loadURI(uri):
    if uri.startswith("data:"):
        base64String = uri.split(",")[1]
        return base64.b64decode(base64String)
    with open(uri, "rb") as f:
        return f.read()
