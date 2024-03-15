// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "obj.h"
// tiny_obj_loader
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
// stb_image
#include "stb_image.h"
// std
#include <sstream>
#include <unordered_map>

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

using TextureCache = std::unordered_map<std::string, anari::Sampler>;

static void loadTexture(anari::Device d,
    anari::Material m,
    std::string filename,
    TextureCache &cache)
{
  std::transform(
      filename.begin(), filename.end(), filename.begin(), [](char c) {
        return c == '\\' ? '/' : c;
      });

  anari::Sampler colorTex = cache[filename];

  if (!colorTex) {
    int width, height, n;
    stbi_set_flip_vertically_on_load(1);
    void *data = stbi_loadf(filename.c_str(), &width, &height, &n, 0);

    if (!data || n < 1) {
      if (!data)
        printf("failed to load texture '%s'\n", filename.c_str());
      else
        printf(
            "texture '%s' with %i channels not loaded\n", filename.c_str(), n);
      return;
    }

    colorTex = anari::newObject<anari::Sampler>(d, "image2D");

    int texelType = ANARI_FLOAT32_VEC4;
    if (n == 3)
      texelType = ANARI_FLOAT32_VEC3;
    else if (n == 2)
      texelType = ANARI_FLOAT32_VEC2;
    else if (n == 1)
      texelType = ANARI_FLOAT32;

    anari::setParameterArray2D(
        d, colorTex, "image", texelType, data, width, height);

    anari::setParameter(d, colorTex, "inAttribute", "attribute0");
    anari::setParameter(d, colorTex, "wrapMode1", "repeat");
    anari::setParameter(d, colorTex, "wrapMode2", "repeat");
    anari::setParameter(d, colorTex, "filter", "linear");
    anari::commitParameters(d, colorTex);
  }

  cache[filename] = colorTex;
  anari::setParameter(d, m, "color", colorTex);
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

  if (!retval) {
    std::stringstream ss;
    ss << "failed to open/parse obj file '" << fileName << "'";
    throw std::runtime_error(ss.str());
  }

  /////////////////////////////////////////////////////////////////////////////

  std::vector<ANARIMaterial> materials;

  auto defaultMaterial = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(
      d, defaultMaterial, "color", math::float3(0.f, 1.f, 0.f));
  anari::commitParameters(d, defaultMaterial);

  TextureCache cache;

  for (auto &mat : objdata.materials) {
    auto m = anari::newObject<anari::Material>(d, "matte");

    anari::setParameter(d, m, "color", ANARI_FLOAT32_VEC3, &mat.diffuse[0]);
    anari::setParameter(d, m, "opacity", ANARI_FLOAT32, &mat.dissolve);
    anari::setParameter(d, m, "alphaMode", "blend");

    if (!mat.diffuse_texname.empty())
      loadTexture(d, m, basePath + mat.diffuse_texname, cache);

#if 1
    if (!mat.alpha_texname.empty())
      loadTexture(d, m, basePath + mat.alpha_texname, cache);
#endif

    anari::commitParameters(d, m);
    materials.push_back(m);
  }

  for (auto &t : cache)
    anari::release(d, t.second);

  /////////////////////////////////////////////////////////////////////////////

  std::vector<anari::Surface> meshes;

  auto *vertices = objdata.attrib.vertices.data();
  auto *texcoords = objdata.attrib.texcoords.data();

  std::vector<math::float3> v;
  std::vector<math::float2> vt;

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

    anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());

    bool allTexCoordsValid = vt.size() == v.size();
    if (allTexCoordsValid) {
      anari::setParameterArray1D(
          d, geom, "vertex.attribute0", vt.data(), vt.size());
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

  auto instance = anari::newObject<anari::Instance>(d, "transform");
  auto group = anari::newObject<anari::Group>(d);

  anari::setParameterArray1D(
      d, group, "surface", ANARI_SURFACE, meshes.data(), meshes.size());

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
      // clang-format off
      {makeParameterInfo("fileName", ".obj file to open", "")}
      // clang-format on
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

  std::string filename = getParamString("fileName", "");
  loadObj(m_device, filename, &m_world);
}

TestScene *sceneFileObj(anari::Device d)
{
  return new FileObj(d);
}

} // namespace scenes
} // namespace anari
