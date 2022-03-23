// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "cornell_box_quad_geom.h"

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

static std::vector<glm::uvec4> indices = {
    {0, 1, 2, 3}, // Floor
    {4, 5, 6, 7}, // Ceiling
    {8, 9, 10, 11}, // Backwall
    {12, 13, 14, 15}, // RightWall
    {16, 17, 18, 19}, // LeftWall
    {20, 21, 22, 23}, // ShortBox Top Face
    {24, 25, 26, 27}, // ShortBox Left Face
    {28, 29, 30, 31}, // ShortBox Front Face
    {32, 33, 34, 35}, // ShortBox Right Face
    {36, 37, 38, 39}, // ShortBox Back Face
    {40, 41, 42, 43}, // ShortBox Bottom Face
    {44, 45, 46, 47}, // TallBox Top Face
    {48, 49, 50, 51}, // TallBox Left Face
    {52, 53, 54, 55}, // TallBox Front Face
    {56, 57, 58, 59}, // TallBox Right Face
    {60, 61, 62, 63}, // TallBox Back Face
    {64, 65, 66, 67}, // TallBox Bottom Face
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

CornellBoxQuadGeometry::CornellBoxQuadGeometry(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

CornellBoxQuadGeometry::~CornellBoxQuadGeometry()
{
  anari::release(m_device, m_world);
}

anari::World CornellBoxQuadGeometry::world()
{
  return m_world;
}

void CornellBoxQuadGeometry::commit()
{
  auto d = m_device;

  auto geom = anari::newObject<anari::Geometry>(d, "quad");

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

  anari::Light light;

  if (anari::deviceImplements(d, "ANARI_KHR_AREA_LIGHTS")) {
    light = anari::newObject<anari::Light>(d, "quad");
    anari::setParameter(d, light, "color", glm::vec3(0.78f, 0.551f, 0.183f));
    anari::setParameter(d, light, "intensity", 47.f);
    anari::setParameter(d, light, "position", glm::vec3(-0.23f, 0.98f, -0.16f));
    anari::setParameter(d, light, "edge1", glm::vec3(0.47f, 0.0f, 0.0f));
    anari::setParameter(d, light, "edge2", glm::vec3(0.0f, 0.0f, 0.38f));
  } else {
    light = anari::newObject<anari::Light>(d, "directional");
    anari::setParameter(d, light, "direction", glm::vec3(0.f, -0.5f, 1.f));
  }

  anari::commit(d, light);

  anari::setAndReleaseParameter(
      d, m_world, "light", anari::newArray1D(d, &light));

  anari::release(d, light);

  anari::setAndReleaseParameter(
      d, m_world, "light", anari::newArray1D(d, &light));

  //anari::commit(d, cam);
  //anari::release(d, cam);

  anari::commit(d, m_world);
}

TestScene *sceneCornellBoxQuadGeometry(anari::Device d)
{
  return new CornellBoxQuadGeometry(d);
}

} // namespace scenes
} // namespace anari
