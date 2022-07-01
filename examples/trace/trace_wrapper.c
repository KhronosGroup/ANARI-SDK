#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "anari/anari.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  (void)userData;
  (void)device;
  (void)source;
  (void)sourceType;
  (void)code;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    fprintf(stderr, "[FATAL] %s\n", message);
  } else if (severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ERROR] %s\n", message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    fprintf(stderr, "[WARN ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
    fprintf(stderr, "[PERF ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_INFO) {
    fprintf(stderr, "[INFO ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_DEBUG) {
    fprintf(stderr, "[DEBUG] %s\n", message);
  }
}

char *raw_data = NULL;

int load_whole_file(const char *name) {
    FILE *fp = fopen(name, "rb");
    if(fp) {
        fseek(fp, 0, SEEK_END);
        uint64_t size = ftell(fp);
        rewind(fp);
        raw_data = malloc(size);
        fread(raw_data, 1, size, fp);
        fclose(fp);
        return 0;
    } else {
        return 1;
    }
}

void* data(uint64_t offset, uint64_t size) {
    return raw_data+offset;
}

void image(const char *channel, const void *pixels, int width, int height, ANARIDataType type) {
    static int count = 0;
    count += 1;
    char filename[100];
    snprintf(filename, sizeof(filename), "%s%d.png", channel, count);
    if(strncmp(channel, "color",5) == 0) {
        stbi_flip_vertically_on_write(1);
        stbi_write_png(filename, width, height, 4, pixels, 4*width);
    }
}

void deleter(const void *userdata, const void *memory) { }
const void *deleterData = NULL;

void trace(ANARIDevice device) {

#include "out.c"

}

int main(int argc, char *argv[]) {

    if(load_whole_file("data.bin")) {
        fprintf(stderr, "ERROR: could not open data file.\n");
        return 1;
    }

    ANARILibrary library = anariLoadLibrary("example", statusFunc, NULL);
    ANARIDevice device = anariNewDevice(library, "default");

    if (!device) {
        fprintf(stderr, "ERROR: could not create device\n");
        return 1;
    }

    trace(device);

    anariRelease(device, device);
    anariUnloadLibrary(library);

    free(raw_data);
}
