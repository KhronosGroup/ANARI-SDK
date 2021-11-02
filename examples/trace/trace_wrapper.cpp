#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>

#include "anari/anari.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


static void errorCallback(void *userData, ANARIError err, const char *errorDetails) {
  (void)userData;
  std::cerr << errorDetails << std::endl;
}

static void statusCallback(void *userData, const char *statusDetails) {
  (void)userData;
  std::cout << statusDetails << std::endl;
}

std::vector<std::unique_ptr<char>> allocations;

void* loadDataFromFile(const char *filename, size_t size) {
    std::string full_name = std::string(TRACE_PATH) + "/" + filename;
    std::ifstream in(full_name, std::ios::in | std::ios::binary);
    if(!in) {
        std::cerr << "failed to open: " << full_name << std::endl;
    }
    char *data = new char[size];
    allocations.emplace_back(data);
    in.read(data, size);
    return data;
}

void writeImage(const char *name, const void *pixels, int width, int height, ANARIFrameFormat mode, std::string channel) {
    static int count = 0;
    if(channel == "color") {
        stbi_flip_vertically_on_write(1);
        stbi_write_png((std::string(name) + "_" + std::to_string(count++) + ".png").c_str(), width, height, 4, pixels, 0);
    }
}

void trace(ANARIDevice device) {
    void* errorCallbackUserPtr = nullptr;
    void* statusCallbackUserPtr = nullptr;

#include "trace.c"

}

int main(int argc, char *argv[]) {

    ANARIDevice device = anariNewDevice("environment");

    if (!device) {
        std::cout << "ERROR: could not create device\n";
        return 1;
    }

    trace(device);
}
