// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "cornell_box_Ring.h"

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

CornellBoxRing::CornellBoxRing(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

CornellBoxRing::~CornellBoxRing()
{
  anari::release(m_device, m_world);
}

anari::World CornellBoxRing::world()
{
  return m_world;
}

std::vector<ParameterInfo> CornellBoxRing::parameters()
{
  return {
      {"intensity", ANARI_FLOAT32, 5.f, ""},
      {"openingAngle", ANARI_FLOAT32, 3.14f, ""},
      {"falloffAngle", ANARI_FLOAT32, 0.0f, ""},
      {"radius", ANARI_FLOAT32, 1.f, ""},
      {"innerRadius", ANARI_FLOAT32, 0.f, ""},
      {"radiance", ANARI_FLOAT32, 1.5f, ""},
      {"R", ANARI_FLOAT32, 0.220f, ""},
      {"G", ANARI_FLOAT32, 0.220f, ""},
      {"B", ANARI_FLOAT32, 0.150f, ""},
      {"X", ANARI_FLOAT32, 0.0f, ""},
      {"Y", ANARI_FLOAT32, 0.0f, ""},
      {"Z", ANARI_FLOAT32, 0.0f, ""}, 
      {"dirX", ANARI_FLOAT32, 0.0f, ""},
      {"dirY", ANARI_FLOAT32, 0.0f, ""},
      {"dirZ", ANARI_FLOAT32, 1.0f, ""}
      //
  };
}

void CornellBoxRing::commit()
{
  auto d = m_device;

  float intensity = getParam<float>("intensity", 5.f);
  float openingAngle = getParam<float>("openingAngle", 3.14f);
  float falloffAngle = getParam<float>("falloffAngle", 0.0f);
  float radius = getParam<float>("radius", 1.f);
  float innerRadius = getParam<float>("innerRadius", 0.f);
  float radiance = getParam<float>("radiance", 1.5f);

  float red = getParam<float>("R", 0.22f);
  float green = getParam<float>("G", 0.22f);
  float blue = getParam<float>("B", 0.15f);

  float X = getParam<float>("X", 0.0f);
  float Y = getParam<float>("Y", 0.0f);
  float Z = getParam<float>("Z", 0.0f);

  float dirX = getParam<float>("dirX", 0.0f);
  float dirY = getParam<float>("dirY", 0.0f);
  float dirZ = getParam<float>("dirZ", 1.0f);

  anari::Light light;

  light = anari::newObject<anari::Light>(d, "ring");
  anari::setParameter(d, light, "color", glm::vec3(red, green, blue));
  anari::setParameter(d, light, "position", glm::vec3(X, Y, Z));
  anari::setParameter(d, light, "direction", glm::vec3(dirX, dirY, dirZ));
  anari::setParameter(d, light, "openingAngle", openingAngle);
  anari::setParameter(d, light, "falloffAngle", falloffAngle);
  anari::setParameter(d, light, "intensity", intensity);
  //anari::setParameter(d, light, "power", 50.f);
  anari::setParameter(d, light, "radius", radius);
  anari::setParameter(d, light, "innerRadius", innerRadius);
  anari::setParameter(d, light, "radiance", radiance);

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

  anari::commit(d, light);

  anari::setAndReleaseParameter(
      d, m_world, "light", anari::newArray1D(d, &light));

  anari::release(d, light);

  anari::commit(d, m_world);
}

TestScene *sceneCornellBoxRing(anari::Device d)
{
  return new CornellBoxRing(d);
}

} // namespace scenes
} // namespace anari
