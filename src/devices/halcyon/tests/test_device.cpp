// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The device created directly through anariNewHalcyonDevice() (linked, not
// loaded as a library) uses the given status callback and reports the device
// properties: version, version.major/minor/patch, version.name(.size) (the ANARI
// SDK's version, 0.17.0), anariVersion.major/minor and geometryMaxIndex, for both
// ANARI_WAIT and ANARI_NO_WAIT.

// halcyon
#include <anari/ext/halcyon/anariNewHalcyonDevice.h>
// std
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {

// messages of any severity sent with the user pointer given at creation
int g_numMessages = 0;
// device errors fail the test
bool g_deviceError = false;

void statusFunc(const void *userPtr,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (userPtr == &g_numMessages)
    g_numMessages++;
  if (severity <= ANARI_SEVERITY_ERROR)
    g_deviceError = true;
  if (severity <= ANARI_SEVERITY_WARNING)
    std::fprintf(stderr, "[ANARI] %s\n", message);
}

bool report(const std::string &test, bool pass)
{
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test.c_str());
  return pass;
}

template <typename T>
bool getProperty(anari::Device d, const char *name, T &value, uint32_t mask)
{
  return anari::getProperty(d, d, name, value, mask);
}

bool testProperties(anari::Device d, uint32_t mask)
{
  const std::string suffix = mask == ANARI_WAIT ? " (WAIT)" : " (NO_WAIT)";
  bool pass = true;

  int version = -1, major = -1, minor = -1, patch = -1;
  bool found = getProperty(d, "version", version, mask)
      && getProperty(d, "version.major", major, mask)
      && getProperty(d, "version.minor", minor, mask)
      && getProperty(d, "version.patch", patch, mask);
  pass &= report("version properties found" + suffix, found);
  // The literal is pinned on purpose (not taken from CMake), so a mis-wired
  // version fails here; bump it with the SDK's version
  pass &= report("version is 0.17.0" + suffix,
      found && major == 0 && minor == 17 && patch == 0);
  pass &= report("version is major * 10000 + minor * 100 + patch" + suffix,
      found && version == major * 10000 + minor * 100 + patch);

  const std::string expectedName = "halcyon 0.17.0";

  uint64_t nameSize = 0;
  pass &= report("version.name.size counts the terminator" + suffix,
      getProperty(d, "version.name.size", nameSize, mask)
          && nameSize == expectedName.size() + 1);

  char name[64];
  std::memset(name, 'x', sizeof(name));
  pass &= report("version.name is '" + expectedName + "'" + suffix,
      anariGetProperty(d, d, "version.name", ANARI_STRING, name, nameSize, mask)
          && std::string(name) == expectedName);

  // A short buffer gets a truncated, still terminated, name
  char shortName[8];
  std::memset(shortName, 'x', sizeof(shortName));
  pass &= report("version.name truncates to the buffer" + suffix,
      anariGetProperty(d,
          d,
          "version.name",
          ANARI_STRING,
          shortName,
          sizeof(shortName),
          mask)
          && std::string(shortName) == expectedName.substr(0, 7));

  int anariMajor = -1, anariMinor = -1;
  pass &= report("anariVersion is 1.1" + suffix,
      getProperty(d, "anariVersion.major", anariMajor, mask)
          && getProperty(d, "anariVersion.minor", anariMinor, mask)
          && anariMajor == 1 && anariMinor == 1);

  uint64_t maxIndex = 0;
  pass &= report("geometryMaxIndex is the uint32 max" + suffix,
      getProperty(d, "geometryMaxIndex", maxIndex, mask)
          && maxIndex == std::numeric_limits<uint32_t>::max());

  int bogus = 0;
  pass &= report("unknown properties aren't found" + suffix,
      !getProperty(d, "notAProperty", bogus, mask));

  return pass;
}

} // namespace

int main()
{
  anari::Device d = anariNewHalcyonDevice(statusFunc, &g_numMessages);
  if (!d) {
    std::fprintf(stderr, "anariNewHalcyonDevice() returned null\n");
    return 1;
  }

  bool pass = true;

  pass &= testProperties(d, ANARI_WAIT);
  pass &= testProperties(d, ANARI_NO_WAIT);

  // The device reports through the callback and user pointer it was given
  // (creating the first object initializes the device, which logs that)
  auto world = anari::newObject<anari::World>(d);
  anari::release(d, world);
  pass &= report("status callback gets the user pointer", g_numMessages > 0);

  anari::release(d, d);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
