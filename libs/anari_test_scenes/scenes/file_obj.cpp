// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "file_obj.h"
// tiny_obj_loader
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
// stb_image
#include "stb_image.h"

namespace anari {
namespace scenes {

static std::string pathOf(const std::string &filename)
{
#ifdef _WIN32
  const char path_sep = '\\';
#else
  const char path_sep = '/';
#endif

  size_t pos = filename.find_last_of(path_sep);
  if (pos == std::string::npos)
    return "";
  return filename.substr(0, pos + 1);
}

struct OBJData
{
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
};

static void loadObj(
    anari::Device d, const std::string &fileName, anari::World *_world)
{
  auto &world = *_world;

  /////////////////////////////////////////////////////////////////////////////

  OBJData objdata;
  std::string warn;
  std::string err;
  std::string basePath = pathOf(fileName);

  auto retval = tinyobj::LoadObj(&objdata.attrib,
      &objdata.shapes,
      &objdata.materials,
      &warn,
      &err,
      fileName.c_str(),
      basePath.c_str(),
      true);

  if (!retval)
    throw std::runtime_error("failed to open/parse obj file!");

  /////////////////////////////////////////////////////////////////////////////

  std::vector<ANARIMaterial> materials;

  auto defaultMaterial = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, defaultMaterial);

  for (auto &mat : objdata.materials) {
    auto m = anari::newObject<anari::Material>(d, "transparentMatte");

    anari::setParameter(d, m, "color", ANARI_FLOAT32_VEC3, &mat.diffuse[0]);
    anari::setParameter(d, m, "opacity", mat.dissolve);
    anari::commitParameters(d, m);

    materials.push_back(m);
  }

  std::vector<anari::Surface> meshes;

  auto *vertices = objdata.attrib.vertices.data();
  auto *texcoords = objdata.attrib.texcoords.data();

  std::vector<glm::vec3> v;
  std::vector<glm::vec2> vt;

  for (auto &shape : objdata.shapes) {
    v.clear();
    vt.clear();

    size_t numIndices = shape.mesh.indices.size();

    v.reserve(numIndices);
    vt.reserve(numIndices);

    for (size_t i = 0; i < numIndices; i += 3) {
      const auto i0 = shape.mesh.indices[i + 0].vertex_index;
      const auto i1 = shape.mesh.indices[i + 1].vertex_index;
      const auto i2 = shape.mesh.indices[i + 2].vertex_index;

      const auto *v0 = vertices + (i0 * 3);
      const auto *v1 = vertices + (i1 * 3);
      const auto *v2 = vertices + (i2 * 3);
      v.emplace_back(v0[0], v0[1], v0[2]);
      v.emplace_back(v1[0], v1[1], v1[2]);
      v.emplace_back(v2[0], v2[1], v2[2]);

      if (texcoords) {
        const auto ti0 = shape.mesh.indices[i + 0].texcoord_index;
        const auto ti1 = shape.mesh.indices[i + 1].texcoord_index;
        const auto ti2 = shape.mesh.indices[i + 2].texcoord_index;
        const auto *t0 = texcoords + (ti0 * 2);
        const auto *t1 = texcoords + (ti1 * 2);
        const auto *t2 = texcoords + (ti2 * 2);
        if (ti0 >= 0)
          vt.emplace_back(t0[0], t0[1]);
        if (ti1 >= 0)
          vt.emplace_back(t1[0], t1[1]);
        if (ti2 >= 0)
          vt.emplace_back(t2[0], t2[1]);
      }
    }

    auto geom = anari::newObject<anari::Geometry>(d, "triangle");

    anari::setAndReleaseParameter(
        d, geom, "vertex.position", anari::newArray1D(d, v.data(), v.size()));

    bool allTexCoordsValid = vt.size() == v.size();
    if (allTexCoordsValid) {
      anari::setAndReleaseParameter(d,
          geom,
          "vertex.attribute0",
          anari::newArray1D(d, vt.data(), vt.size()));
    }

    anari::commitParameters(d, geom);

    auto surface = anari::newObject<anari::Surface>(d);

    int matID = shape.mesh.material_ids[0];
    auto mat = matID < 0 ? defaultMaterial : materials[size_t(matID)];
    anari::setParameter(d, surface, "material", mat);
    anari::setParameter(d, surface, "geometry", geom);

    anari::commitParameters(d, surface);
    anari::release(d, geom);

    meshes.push_back(surface);
  }

  /////////////////////////////////////////////////////////////////////////////

  auto instance = anari::newObject<anari::Instance>(d);
  auto group = anari::newObject<anari::Group>(d);

  anari::setAndReleaseParameter(
      d, group, "surface", anari::newArray1D(d, meshes.data(), meshes.size()));

  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", glm::vec3(-1, -2, -1));
  anari::setParameter(d, light, "irradiance", 4.f);
  anari::commitParameters(d, light);

  anari::setAndReleaseParameter(
      d, group, "light", anari::newArray1D(d, &light));

  anari::release(d, light);

  anari::commitParameters(d, group);

  anari::setAndReleaseParameter(d, instance, "group", group);

  anari::commitParameters(d, instance);

  anari::setAndReleaseParameter(
      d, world, "instance", anari::newArray1D(d, &instance));
  anari::release(d, instance);

  anari::commitParameters(d, world);

  for (auto &m : meshes)
    anari::release(d, m);
  for (auto &m : materials)
    anari::release(d, m);
  anari::release(d, defaultMaterial);
}

// FileObj definitions ////////////////////////////////////////////////////////

FileObj::FileObj(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

FileObj::~FileObj()
{
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> FileObj::parameters()
{
  return {
      {"fileName", ANARI_STRING, std::string(), ".obj file to open."}
      //
  };
}

anari::World FileObj::world()
{
  return m_world;
}

void FileObj::commit()
{
  if (!hasParam("fileName"))
    return;

  std::string filename =
      getParam<std::string>("fileName", getParam<const char *>("fileName", ""));
  loadObj(m_device, filename, &m_world);
}

TestScene *sceneFileObj(anari::Device d)
{
  return new FileObj(d);
}

} // namespace scenes
} // namespace anari
