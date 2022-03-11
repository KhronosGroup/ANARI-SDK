// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "cornell_box_multilight.h"

namespace anari {
namespace scenes {

// quad mesh data
static std::vector<glm::vec3> vertices = {
    // Floor
    {1.00f, -1.00f, -1.00f},
    {-1.00f, -1.00f, -1.00f},
    {-1.00f, -1.00f, 1.00f},
    {1.00f, -1.00f, 1.00f},
    // Ceiling
    {1.00f, 1.00f, -1.00f},
    {1.00f, 1.00f, 1.00f},
    {-1.00f, 1.00f, 1.00f},
    {-1.00f, 1.00f, -1.00f},
    // Backwall
    {1.00f, -1.00f, 1.00f},
    {-1.00f, -1.00f, 1.00f},
    {-1.00f, 1.00f, 1.00f},
    {1.00f, 1.00f, 1.00f},
    // RightWall
    {-1.00f, -1.00f, 1.00f},
    {-1.00f, -1.00f, -1.00f},
    {-1.00f, 1.00f, -1.00f},
    {-1.00f, 1.00f, 1.00f},
    // LeftWall
    {1.00f, -1.00f, -1.00f},
    {1.00f, -1.00f, 1.00f},
    {1.00f, 1.00f, 1.00f},
    {1.00f, 1.00f, -1.00f},
    // ShortBox Top Face
    {-0.53f, -0.40f, -0.75f},
    {-0.70f, -0.40f, -0.17f},
    {-0.13f, -0.40f, -0.00f},
    {0.05f, -0.40f, -0.57f},
    // ShortBox Left Face
    {0.05f, -1.00f, -0.57f},
    {0.05f, -0.40f, -0.57f},
    {-0.13f, -0.40f, -0.00f},
    {-0.13f, -1.00f, -0.00f},
    // ShortBox Front Face
    {-0.53f, -1.00f, -0.75f},
    {-0.53f, -0.40f, -0.75f},
    {0.05f, -0.40f, -0.57f},
    {0.05f, -1.00f, -0.57f},
    // ShortBox Right Face
    {-0.70f, -1.00f, -0.17f},
    {-0.70f, -0.40f, -0.17f},
    {-0.53f, -0.40f, -0.75f},
    {-0.53f, -1.00f, -0.75f},
    // ShortBox Back Face
    {-0.13f, -1.00f, -0.00f},
    {-0.13f, -0.40f, -0.00f},
    {-0.70f, -0.40f, -0.17f},
    {-0.70f, -1.00f, -0.17f},
    // ShortBox Bottom Face
    {-0.53f, -1.00f, -0.75f},
    {-0.70f, -1.00f, -0.17f},
    {-0.13f, -1.00f, -0.00f},
    {0.05f, -1.00f, -0.57f},
    // TallBox Top Face
    {0.53f, 0.20f, -0.09f},
    {-0.04f, 0.20f, 0.09f},
    {0.14f, 0.20f, 0.67f},
    {0.71f, 0.20f, 0.49f},
    // TallBox Left Face
    {0.53f, -1.00f, -0.09f},
    {0.53f, 0.20f, -0.09f},
    {0.71f, 0.20f, 0.49f},
    {0.71f, -1.00f, 0.49f},
    // TallBox Front Face
    {0.71f, -1.00f, 0.49f},
    {0.71f, 0.20f, 0.49f},
    {0.14f, 0.20f, 0.67f},
    {0.14f, -1.00f, 0.67f},
    // TallBox Right Face
    {0.14f, -1.00f, 0.67f},
    {0.14f, 0.20f, 0.67f},
    {-0.04f, 0.20f, 0.09f},
    {-0.04f, -1.00f, 0.09f},
    // TallBox Back Face
    {-0.04f, -1.00f, 0.09f},
    {-0.04f, 0.20f, 0.09f},
    {0.53f, 0.20f, -0.09f},
    {0.53f, -1.00f, -0.09f},
    // TallBox Bottom Face
    {0.53f, -1.00f, -0.09f},
    {-0.04f, -1.00f, 0.09f},
    {0.14f, -1.00f, 0.67f},
    {0.71f, -1.00f, 0.49f}};

static std::vector<glm::uvec3> indices = {
    {0, 1, 2}, // Floor
    {0, 2, 3}, // Floor
    {4, 5, 6}, // Ceiling
    {4, 6, 7}, // Ceiling
    {8, 9, 10}, // Backwall
    {8, 10, 11}, // Backwall
    {12, 13, 14}, // RightWall
    {12, 14, 15}, // RightWall
    {16, 17, 18}, // LeftWall
    {16, 18, 19}, // LeftWall
    {20, 22, 23}, // ShortBox Top Face
    {20, 21, 22}, // ShortBox Top Face
    {24, 25, 26}, // ShortBox Left Face
    {24, 26, 27}, // ShortBox Left Face
    {28, 29, 30}, // ShortBox Front Face
    {28, 30, 31}, // ShortBox Front Face
    {32, 33, 34}, // ShortBox Right Face
    {32, 34, 35}, // ShortBox Right Face
    {36, 37, 38}, // ShortBox Back Face
    {36, 38, 39}, // ShortBox Back Face
    {40, 41, 42}, // ShortBox Bottom Face
    {40, 42, 43}, // ShortBox Bottom Face
    {44, 45, 46}, // TallBox Top Face
    {44, 46, 47}, // TallBox Top Face
    {48, 49, 50}, // TallBox Left Face
    {48, 50, 51}, // TallBox Left Face
    {52, 53, 54}, // TallBox Front Face
    {52, 54, 55}, // TallBox Front Face
    {56, 57, 58}, // TallBox Right Face
    {56, 58, 59}, // TallBox Right Face
    {60, 61, 62}, // TallBox Back Face
    {60, 62, 63}, // TallBox Back Face
    {64, 65, 66}, // TallBox Bottom Face
    {64, 66, 67}, // TallBox Bottom Face
};

static std::vector<glm::vec4> colors = {
    // Floor
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // Ceiling
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // Backwall
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // RightWall
    {0.140f, 0.450f, 0.091f, 1.0f},
    {0.140f, 0.450f, 0.091f, 1.0f},
    {0.140f, 0.450f, 0.091f, 1.0f},
    {0.140f, 0.450f, 0.091f, 1.0f},
    // LeftWall
    {0.630f, 0.065f, 0.05f, 1.0f},
    {0.630f, 0.065f, 0.05f, 1.0f},
    {0.630f, 0.065f, 0.05f, 1.0f},
    {0.630f, 0.065f, 0.05f, 1.0f},
    // ShortBox Top Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // ShortBox Left Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // ShortBox Front Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // ShortBox Right Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // ShortBox Back Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // ShortBox Bottom Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // TallBox Top Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // TallBox Left Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // TallBox Front Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // TallBox Right Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // TallBox Back Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    // TallBox Bottom Face
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f},
    {0.725f, 0.710f, 0.68f, 1.0f}};

// CornelBox definitions //////////////////////////////////////////////////////

CornellBoxMultilight::CornellBoxMultilight(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

CornellBoxMultilight::~CornellBoxMultilight()
{
  anari::release(m_device, m_world);
}

anari::World CornellBoxMultilight::world()
{
  return m_world;
}

std::vector<ParameterInfo> CornellBoxMultilight::parameters()
{
  return {
      {"Spot intensity", ANARI_FLOAT32, 20.f, ""},
      {"Spot openingAngle", ANARI_FLOAT32, 0.5f, ""},
      {"Spot falloffAngle", ANARI_FLOAT32, 0.f, ""},
      {"Spot power", ANARI_FLOAT32, 50.f, ""},
      {"Spot R", ANARI_FLOAT32, 0.255f, ""},
      {"Spot G", ANARI_FLOAT32, 0.f, ""},
      {"Spot B", ANARI_FLOAT32, 0.f, ""},
      {"Spot X", ANARI_FLOAT32, 0.0f, ""},
      {"Spot Y", ANARI_FLOAT32, 0.0f, ""},
      {"Spot Z", ANARI_FLOAT32, -2.0f, ""},
      {"Spot dirX", ANARI_FLOAT32, 0.0f, ""},
      {"Spot dirY", ANARI_FLOAT32, 0.0f, ""},
      {"Spot dirZ", ANARI_FLOAT32, 1.0f, ""},
      {"Quad intensity", ANARI_FLOAT32, 20.f, ""},
      {"Quad radiance", ANARI_FLOAT32, 0.5f, ""},
      {"Quad power", ANARI_FLOAT32, 100.f, ""},
      {"Quad Double Sided", ANARI_BOOL, true, ""},
      {"Quad R", ANARI_FLOAT32, 0.f, ""},
      {"Quad G", ANARI_FLOAT32, 0.f, ""},
      {"Quad B", ANARI_FLOAT32, 0.255f, ""},
      {"Quad X", ANARI_FLOAT32, -0.23f, ""},
      {"Quad Y", ANARI_FLOAT32, 0.98f, ""},
      {"Quad Z", ANARI_FLOAT32, -0.16f, ""}
      //
  };
}

void CornellBoxMultilight::commit()
{
  auto d = m_device;

  float spot_intensity = getParam<float>("Spot intensity", 20.f);
  float spot_openingAngle = getParam<float>("Spot openingAngle", 0.5f);
  float spot_falloffAngle = getParam<float>("Spot falloffAngle", 0.f);
  float spot_power = getParam<float>("Spot power", 50.f);

  float spot_red = getParam<float>("Spot R", 0.255f);
  float spot_green = getParam<float>("Spot G", 0.0f);
  float spot_blue = getParam<float>("Spot B", 0.0f);

  float spot_X = getParam<float>("Spot X", 0.0f);
  float spot_Y = getParam<float>("Spot Y", 0.0f);
  float spot_Z = getParam<float>("Spot Z", -2.0f);

  float spot_dirX = getParam<float>("Spot dirX", 0.0f);
  float spot_dirY = getParam<float>("Spot dirY", 0.0f);
  float spot_dirZ = getParam<float>("Spot dirZ", 1.0f);

  float quad_intensity = getParam<float>("Quad intensity", 20.f);
  float quad_radiance = getParam<float>("Quad radiance", 0.5f);
  float quad_power = getParam<float>("Quad power", 100.f);
  bool double_sided = getParam<float>("Quad Double Sided", true);

  float quad_red = getParam<float>("Quad R", 0.f);
  float quad_green = getParam<float>("Quad G", 0.f);
  float quad_blue = getParam<float>("Quad B", 0.255f);

  float quad_X = getParam<float>("Quad X", -0.23f);
  float quad_Y = getParam<float>("Quad Y", 0.98f);
  float quad_Z = getParam<float>("Quad Z", -0.16f);

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");

  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, vertices.data(), vertices.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.color",
      anari::newArray1D(d, colors.data(), colors.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "primitive.index",
      anari::newArray1D(d, indices.data(), indices.size()));

  anari::commit(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commit(d, mat);
  anari::setAndReleaseParameter(d, surface, "material", mat);

  anari::commit(d, surface);

  anari::setAndReleaseParameter(
      d, m_world, "surface", anari::newArray1D(d, &surface));
  anari::release(d, surface);

  anari::Light light1;
  anari::Light light2;


  light1 = anari::newObject<anari::Light>(d, "spot");
  anari::setParameter(
      d, light1, "color", glm::vec3(spot_red, spot_green, spot_blue));
  anari::setParameter(d, light1, "position", glm::vec3(spot_X, spot_Y, spot_Z));
  anari::setParameter(
      d, light1, "direction", glm::vec3(spot_dirX, spot_dirY, spot_dirZ));
  anari::setParameter(d, light1, "openingAngle", spot_openingAngle);
  anari::setParameter(d, light1, "falloffAngle", spot_falloffAngle);
  anari::setParameter(d, light1, "intensity", spot_intensity);
  anari::setParameter(
      d, light1, "power", 50.f); // intensity takes precedence if also specified

  light2 = anari::newObject<anari::Light>(d, "quad");
  anari::setParameter(d, light2, "color", glm::vec3(quad_red, quad_green, quad_blue));
  anari::setParameter(d, light2, "intensity", quad_intensity);
  anari::setParameter(d, light2, "position", glm::vec3(quad_X, quad_Y, quad_Z));
  anari::setParameter(d, light2, "edge1", glm::vec3(0.47f, 0.0f, 0.0f));
  anari::setParameter(d, light2, "edge2", glm::vec3(0.0f, 0.0f, 0.38f));
  anari::setParameter(d,
      light2,
      "power",
      quad_power); // intensity takes precedence if also specified
  anari::setParameter(d, light2, "radiance", quad_radiance);
  anari::setParameter(d, light2, "side", double_sided ? "both" : "front");

  std::vector<anari::Light> light_array = {light1, light2};

  anari::commit(d, light1);
  anari::commit(d, light2);

  anari::setAndReleaseParameter(
      d, m_world, "light", anari::newArray1D(d, light_array.data(), light_array.size()));

  anari::release(d, light1);
  anari::release(d, light2);

  anari::commit(d, m_world);
}

TestScene *sceneCornellBoxMultilight(anari::Device d)
{
  return new CornellBoxMultilight(d);
}

} // namespace scenes
} // namespace anari
