// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "MaterialXDocument.h"
#include "ColorManagement.h"
#include "HalcyonTextureSystem.h"
#include "MaterialXLibrary.h"
#include "PathMaterial.h"
// MaterialX
#include <MaterialXCore/Material.h>
#include <MaterialXFormat/XmlIo.h>
// typhoon
#include <renderer/materials/MaterialXCpp/graph.h>
#include <renderer/materials/MaterialXCpp/nodeRegistry.h>
// std
#include <filesystem>
#include <mutex>
#include <set>
#include <unordered_map>

namespace halcyon::pathtracer {

namespace mx = MaterialX;
namespace fs = std::filesystem;

using mxcpp::GraphNode;
using mxcpp::MaterialGraph;
using mxcpp::Value;
using mxcpp::Vec2f;
using mxcpp::Vec3f;
using mxcpp::Vec4f;
using Severity = MaterialXDiagnostic::Severity;

namespace {

// Typhoon's mxcppAdapter.cpp renames ////////////////////////////////////////

bool isUsdPrimvarReader(const std::string &type)
{
  return type.rfind("ND_UsdPrimvarReader_", 0) == 0;
}

// _CanonicalNodeTypeId() for the node types a .mtlx can hold (MaterialX's
// UsdPrimvarReader and UsdTransform2d nodedefs; the native USD ids can't be
// in a document)
std::string canonicalNodeType(const std::string &type)
{
  static const std::unordered_map<std::string, std::string> RENAMES = {
      {"ND_UsdPrimvarReader_integer", "ND_geompropvalue_integer"},
      {"ND_UsdPrimvarReader_boolean", "ND_geompropvalue_boolean"},
      {"ND_UsdPrimvarReader_string", "ND_geompropvalueuniform_string"},
      {"ND_UsdPrimvarReader_filename", "ND_geompropvalueuniform_filename"},
      {"ND_UsdPrimvarReader_float", "ND_geompropvalue_float"},
      {"ND_UsdPrimvarReader_vector2", "ND_geompropvalue_vector2"},
      {"ND_UsdPrimvarReader_vector3", "ND_geompropvalue_vector3"},
      {"ND_UsdPrimvarReader_vector4", "ND_geompropvalue_vector4"},
      {"ND_UsdTransform2d", "ND_place2d_vector2"}};
  const auto it = RENAMES.find(type);
  return it == RENAMES.end() ? type : it->second;
}

// _CanonicalInputName()
std::string canonicalInputName(const std::string &type, const std::string &name)
{
  if (isUsdPrimvarReader(type)) {
    if (name == "varname")
      return "geomprop";
    if (name == "fallback")
      return "default";
  }
  if (type == "ND_UsdTransform2d") {
    if (name == "in")
      return "texcoord";
    if (name == "rotation")
      return "rotate";
    if (name == "translation")
      return "offset";
  }
  return name;
}

// Which nodes MaterialXCpp evaluates natively ////////////////////////////////

// Whether MaterialXCpp's graph compilation dispatches node type 'type'
// itself, as a material model (standard_surface, open_pbr_surface, ...) or a
// displacement shader: a one-node graph with it as the terminal compiles.
// The models aren't registry nodes, so this is how the public API tells.
bool isTerminalModel(const std::string &type)
{
  static std::mutex mutex;
  static std::unordered_map<std::string, bool> cache;
  std::lock_guard<std::mutex> lock(mutex);
  const auto it = cache.find(type);
  if (it != cache.end())
    return it->second;
  bool model = false;
  for (const char *terminal : {"surface", "displacement"}) {
    MaterialGraph g;
    g.nodes["node"].nodeTypeId = type;
    g.terminals[terminal] = {"node", "out"};
    model = model
        || mxcpp::EvalGraph::Compile(g, terminal).status
            == mxcpp::CompileStatus::Valid;
  }
  cache[type] = model;
  return model;
}

// Whether MaterialXCpp evaluates nodedef 'nodeDef' (renamed as Typhoon
// renames it): a registered node, or a model it dispatches
bool isNative(const std::string &nodeDef)
{
  const std::string type = canonicalNodeType(nodeDef);
  return mxcpp::NodeRegistry::GetInstance().Find(type) != nullptr
      || isTerminalModel(type);
}

// Values /////////////////////////////////////////////////////////////////////

// A MaterialX value as mxcpp::Value, as mxcppAdapter.cpp's _ConvertValue()
// makes Hydra's: color3 and vector3 are both Vec3f, matrices row by row.
// Empty for types MaterialXCpp has no value of (arrays, ...).
Value toValue(const mx::ValuePtr &v)
{
  if (!v)
    return {};
  if (v->isA<float>())
    return Value(v->asA<float>());
  if (v->isA<int>())
    return Value(v->asA<int>());
  if (v->isA<bool>())
    return Value(v->asA<bool>());
  if (v->isA<mx::Vector2>()) {
    const auto a = v->asA<mx::Vector2>();
    return Value(Vec2f(a[0], a[1]));
  }
  if (v->isA<mx::Vector3>()) {
    const auto a = v->asA<mx::Vector3>();
    return Value(Vec3f(a[0], a[1], a[2]));
  }
  if (v->isA<mx::Color3>()) {
    const auto a = v->asA<mx::Color3>();
    return Value(Vec3f(a[0], a[1], a[2]));
  }
  if (v->isA<mx::Vector4>()) {
    const auto a = v->asA<mx::Vector4>();
    return Value(Vec4f(a[0], a[1], a[2], a[3]));
  }
  if (v->isA<mx::Color4>()) {
    const auto a = v->asA<mx::Color4>();
    return Value(Vec4f(a[0], a[1], a[2], a[3]));
  }
  if (v->isA<mx::Matrix33>()) {
    const auto m = v->asA<mx::Matrix33>();
    mxcpp::Mat3f r;
    for (int row = 0; row < 3; row++)
      for (int col = 0; col < 3; col++)
        r[row][col] = m[row][col];
    return Value(r);
  }
  if (v->isA<mx::Matrix44>()) {
    const auto m = v->asA<mx::Matrix44>();
    mxcpp::Mat4f r;
    for (int row = 0; row < 4; row++)
      for (int col = 0; col < 4; col++)
        r[row][col] = m[row][col];
    return Value(r);
  }
  if (v->isA<std::string>())
    return Value(v->asA<std::string>());
  return {};
}

const std::map<std::string, std::string> COLOR_SPACES = {
    {"lin_rec709", "lin_rec709_scene"},
    {"srgb_texture", "srgb_rec709_scene"},
    {"acescg", "lin_ap1_scene"},
    {"g22_rec709", "g22_rec709_scene"},
    {"g18_rec709", "g18_rec709_scene"},
    {"g22_ap1", "g22_ap1_scene"},
    {"lin_adobergb", "lin_adobergb_scene"},
    {"adobergb", "g22_adobergb_scene"},
    {"lin_displayp3", "lin_p3d65_scene"},
    {"srgb_displayp3", "srgb_p3d65_scene"}};

// The directory relative paths in 'element' resolve against: its document's
// (or included file's), else the working directory
std::string directoryOf(const mx::ConstElementPtr &element)
{
  const std::string uri = element->getActiveSourceUri();
  std::error_code ec;
  const fs::path cwd = fs::current_path(ec);
  if (uri.empty())
    return cwd.string();
  return fs::absolute(fs::path(uri), ec)
      .parent_path()
      .lexically_normal()
      .string();
}

// 'path' made absolute against 'directory', if it is relative
std::string resolvePath(const std::string &path, const std::string &directory)
{
  if (path.empty())
    return path;
  const fs::path p(path);
  if (p.is_absolute())
    return p.lexically_normal().string();
  return (fs::path(directory) / p).lexically_normal().string();
}

// Conversion /////////////////////////////////////////////////////////////////

// Where an input's value comes from: an upstream node's output, or the
// element holding the value (the input itself, the input of an enclosing
// node graph it names with 'interfacename', or a node graph output)
struct Source
{
  mx::NodePtr node;
  std::string output;
  mx::ValueElementPtr value;
};

class Converter
{
 public:
  explicit Converter(MaterialXMaterial &result) : m_result(result) {}

  // Converts 'node' and everything upstream; its key in the graph
  std::string visit(const mx::NodePtr &node);

  void diagnose(Severity s, const std::string &message)
  {
    if (m_reported.insert(message).second)
      m_result.diagnostics.push_back({s, message});
  }

 private:
  Source resolve(mx::InputPtr in, int depth = 0);
  Source resolveOutput(const mx::OutputPtr &out, int depth);
  void convertInput(GraphNode &node,
      const std::string &nodeType,
      const std::string &nodeKey,
      const mx::InputPtr &in);

  MaterialXMaterial &m_result;
  std::set<std::string> m_visited;
  std::set<std::string> m_reported;
};

Source Converter::resolve(mx::InputPtr in, int depth)
{
  // a chain of interface names ends at a node graph input; limit the hops
  // for documents that loop
  for (; depth < 32 && in->hasInterfaceName(); depth++) {
    mx::InputPtr outer = in->getInterfaceInput();
    if (!outer)
      break;
    in = outer;
  }
  if (in->hasNodeGraphString()) {
    if (mx::OutputPtr out = in->getConnectedOutput())
      return resolveOutput(out, depth + 1);
    return {nullptr, "", nullptr};
  }
  if (in->hasNodeName()) {
    mx::NodePtr node = in->getConnectedNode();
    return {node,
        in->hasOutputString() ? in->getOutputString() : std::string("out"),
        nullptr};
  }
  if (in->hasOutputString()) {
    // an output at document scope
    if (mx::OutputPtr out = in->getConnectedOutput())
      return resolveOutput(out, depth + 1);
  }
  return {nullptr, "", in};
}

Source Converter::resolveOutput(const mx::OutputPtr &out, int depth)
{
  if (depth >= 32)
    return {nullptr, "", nullptr};
  if (out->hasInterfaceName()) {
    mx::ElementPtr parent = out->getParent();
    if (auto graph = parent ? parent->asA<mx::NodeGraph>() : nullptr) {
      if (mx::InputPtr in = graph->getInput(out->getInterfaceName()))
        return resolve(in, depth + 1);
    }
  }
  if (out->hasNodeGraphString()) {
    if (mx::OutputPtr next = out->getConnectedOutput())
      return resolveOutput(next, depth + 1);
  }
  if (out->hasNodeName()) {
    return {out->getConnectedNode(),
        out->hasOutputString() ? out->getOutputString() : std::string("out"),
        nullptr};
  }
  return {nullptr, "", out};
}

void Converter::convertInput(GraphNode &node,
    const std::string &nodeType,
    const std::string &nodeKey,
    const mx::InputPtr &in)
{
  const std::string name = canonicalInputName(nodeType, in->getName());
  const Source src = resolve(in);
  if (src.node) {
    node.inputConnections[name] = {{visit(src.node), src.output}};
    return;
  }
  // unconnected and without a value (closures' empty "" values too)
  if (!src.value || src.value->getValueString().empty())
    return;

  const std::string type = in->getType();
  const mx::ConstElementPtr holder = src.value;
  if (type == mx::FILENAME_TYPE_STRING) {
    // fileprefix and tokens, then the document's directory
    const std::string directory = directoryOf(holder);
    node.parameters[name] =
        Value(resolvePath(src.value->getResolvedValueString(), directory));
    m_result.textureInputs[in->getNamePath()] = {nodeKey, name, directory};
    // the colour space sampled colours convert from (image nodes put it in
    // their requests, HalcyonTextureSystem converts)
    const std::string mxSpace = src.value->getActiveColorSpace();
    const std::string cs = openUsdColorSpace(mxSpace);
    if (!cs.empty() && cs != WORKING_COLOR_SPACE) {
      Vec3f probe(0.5f);
      if (!convertToWorkingSpace(cs, probe)) {
        diagnose(Severity::WARNING,
            "unknown colour space '" + mxSpace + "' of input '"
                + in->getNamePath() + "'; its colours are left unconverted");
      }
      node.parameters["colorSpace:" + name] = Value(cs);
    }
    return;
  }

  Value v = toValue(src.value->getValue());
  if (mxcpp::ValueIsEmpty(v)) {
    diagnose(Severity::WARNING,
        "input '" + in->getNamePath() + "' of type '" + type
            + "' isn't supported; it takes its default");
    return;
  }
  // authored colours in another colour space convert into the working
  // space, as _ConvertAuthoredColorParameters() does
  if (type == "color3" || type == "color4") {
    const std::string mxSpace = src.value->getActiveColorSpace();
    const std::string cs = openUsdColorSpace(mxSpace);
    if (!cs.empty() && cs != WORKING_COLOR_SPACE) {
      Vec3f rgb = mxcpp::ValueHolds<Vec3f>(v)
          ? mxcpp::ValueGet<Vec3f>(v)
          : Vec3f(mxcpp::ValueGet<Vec4f>(v)[0],
                mxcpp::ValueGet<Vec4f>(v)[1],
                mxcpp::ValueGet<Vec4f>(v)[2]);
      if (convertToWorkingSpace(cs, rgb)) {
        if (mxcpp::ValueHolds<Vec3f>(v))
          v = Value(rgb);
        else
          v = Value(
              Vec4f(rgb[0], rgb[1], rgb[2], mxcpp::ValueGet<Vec4f>(v)[3]));
      } else {
        diagnose(Severity::WARNING,
            "unknown colour space '" + mxSpace + "' of input '"
                + in->getNamePath() + "'; its value is left unconverted");
      }
    }
  }
  node.parameters[name] = std::move(v);
}

std::string Converter::visit(const mx::NodePtr &node)
{
  const std::string key = node->getNamePath();
  if (!m_visited.insert(key).second)
    return key;

  GraphNode converted;
  mx::NodeDefPtr def = node->getNodeDef();
  if (def) {
    converted.nodeTypeId = canonicalNodeType(def->getName());
  } else {
    converted.nodeTypeId = node->getCategory();
    diagnose(Severity::WARNING,
        "node '" + key + "' (" + node->getCategory() + ", " + node->getType()
            + ") matches no nodedef");
  }
  const std::string nodeDefName = def ? def->getName() : std::string();
  for (const mx::InputPtr &in : node->getInputs())
    convertInput(converted, nodeDefName, key, in);
  m_result.graph.nodes[key] = std::move(converted);
  return key;
}

// The material nodes with a surface or volume shader
std::vector<mx::NodePtr> materialNodes(const mx::DocumentPtr &doc)
{
  std::vector<mx::NodePtr> nodes;
  for (const mx::NodePtr &m : doc->getMaterialNodes()) {
    if (!mx::getShaderNodes(m, mx::SURFACE_SHADER_TYPE_STRING).empty()
        || !mx::getShaderNodes(m, mx::VOLUME_SHADER_TYPE_STRING).empty())
      nodes.push_back(m);
  }
  return nodes;
}

void error(MaterialXMaterial &m, const std::string &message)
{
  m.valid = false;
  m.diagnostics.push_back({Severity::ERROR, message});
}

} // namespace

std::string openUsdColorSpace(const std::string &name)
{
  if (name.empty() || name == "none" || name == "raw")
    return "";
  const auto it = COLOR_SPACES.find(name);
  return it == COLOR_SPACES.end() ? name : it->second;
}

MaterialXMaterial readMaterialX(MaterialXSourceType type,
    const std::string &source,
    const std::string &materialName)
{
  registerTextureNodes();

  MaterialXMaterial result;
  const bool inline_ = type == MaterialXSourceType::DOCUMENT_INLINE;
  if (source.empty()) {
    error(result, "no 'source'");
    return result;
  }

  mx::DocumentPtr doc;
  try {
    doc = mx::createDocument();
    doc->setDataLibrary(materialXDataLibrary());
    if (inline_) {
      std::error_code ec;
      mx::readFromXmlString(
          doc, source, mx::FileSearchPath(fs::current_path(ec).string()));
    } else {
      const mx::FilePath path(source);
      mx::readFromXmlFile(doc, path, mx::FileSearchPath(path.getParentPath()));
    }
  } catch (const std::exception &e) {
    error(result,
        std::string(inline_ ? "the inline document" : "'" + source + "'")
            + " doesn't read: " + e.what());
    return result;
  }

  try {
    std::string message;
    if (!doc->validate(&message)) {
      if (message.size() > 1000)
        message = message.substr(0, 1000) + "...";
      result.diagnostics.push_back(
          {Severity::WARNING, "the document doesn't validate: " + message});
    }

    // select the material
    const std::vector<mx::NodePtr> materials = materialNodes(doc);
    for (const mx::NodePtr &m : materials)
      result.materialNames.push_back(m->getNamePath());
    mx::NodePtr material;
    if (materialName.empty()) {
      if (!materials.empty())
        material = materials.front();
    } else {
      for (const mx::NodePtr &m : materials) {
        if (m->getNamePath() == materialName || m->getName() == materialName) {
          material = m;
          break;
        }
      }
    }
    if (!material) {
      std::string names;
      for (const std::string &n : result.materialNames)
        names += (names.empty() ? "" : ", ") + n;
      error(result,
          materials.empty()
              ? std::string("the document has no material with a surface or "
                            "volume shader")
              : "no material '" + materialName + "' (the document has " + names
                  + ")");
      return result;
    }
    result.materialName = material->getNamePath();

    // Inline what MaterialXCpp lacks, keeping what it evaluates natively
    // (notably the shading models, which an unfiltered flatten would turn
    // into BSDF networks); flattenSubgraphs() doesn't descend into node
    // graphs, so flatten each too
    const mx::NodePredicate flatten = [](mx::NodePtr n) {
      mx::NodeDefPtr def = n->getNodeDef();
      return !(def && isNative(def->getName()));
    };
    doc->flattenSubgraphs(mx::EMPTY_STRING, flatten);
    for (const mx::NodeGraphPtr &graph : doc->getNodeGraphs())
      graph->flattenSubgraphs(mx::EMPTY_STRING, flatten);

    Converter converter(result);
    const auto shaders = [&](const std::string &shaderType) {
      return mx::getShaderNodes(material, shaderType);
    };
    const std::vector<mx::NodePtr> surface =
        shaders(mx::SURFACE_SHADER_TYPE_STRING);
    const std::vector<mx::NodePtr> volume =
        shaders(mx::VOLUME_SHADER_TYPE_STRING);
    const std::vector<mx::NodePtr> displacement =
        shaders(mx::DISPLACEMENT_SHADER_TYPE_STRING);
    if (!surface.empty()) {
      const mx::NodePtr &shader = surface.front();
      result.shaderNode = converter.visit(shader);
      result.graph.terminals["surface"] = {result.shaderNode, "out"};
      if (mx::NodeDefPtr def = shader->getNodeDef()) {
        result.shaderNodeDef = canonicalNodeType(def->getName());
        for (const mx::InputPtr &in : def->getActiveInputs()) {
          result.shaderInputs[in->getName()] = {
              in->getType(), toValue(in->getValue())};
        }
      }
    }
    if (!volume.empty()) {
      result.graph.terminals["volume"] = {
          converter.visit(volume.front()), "out"};
    }
    if (!displacement.empty()) {
      result.graph.terminals["displacement"] = {
          converter.visit(displacement.front()), "out"};
      const mxcpp::CompileResult compiled =
          mxcpp::EvalGraph::Compile(result.graph, "displacement");
      if (compiled.status == mxcpp::CompileStatus::Valid) {
        result.diagnostics.push_back({Severity::DEBUG,
            "its displacement shader compiles, but halcyon doesn't displace"
            " surfaces"});
      } else {
        result.diagnostics.push_back({Severity::WARNING,
            "its displacement shader doesn't compile (" + compiled.diagnostic
                + "); halcyon doesn't displace surfaces" + " anyway"});
      }
    }
    result.valid = true;
  } catch (const std::exception &e) {
    error(result, std::string("the document doesn't convert: ") + e.what());
  }
  return result;
}

// Overrides //////////////////////////////////////////////////////////////////

namespace {

// The shader input type 'type' a sampler or attribute can set, as
// MaterialConverter's InputType
bool connectableType(const std::string &type, InputType &out)
{
  static const std::map<std::string, InputType> TYPES = {
      {"float", InputType::FLOAT},
      {"vector2", InputType::VECTOR2},
      {"vector3", InputType::VECTOR3},
      {"color3", InputType::COLOR3},
      {"vector4", InputType::VECTOR4},
      {"color4", InputType::COLOR4}};
  const auto it = TYPES.find(type);
  if (it == TYPES.end())
    return false;
  out = it->second;
  return true;
}

// 'v' as a value of MaterialX type 'type' (the ANARI type the input takes),
// empty if it isn't one
Value valueOfType(const Value &v, const std::string &type)
{
  if (type == "float" && mxcpp::ValueHolds<float>(v))
    return v;
  if (type == "integer" && mxcpp::ValueHolds<int>(v))
    return v;
  if (type == "boolean" && mxcpp::ValueHolds<bool>(v))
    return v;
  if (type == "vector2" && mxcpp::ValueHolds<Vec2f>(v))
    return v;
  if ((type == "color3" || type == "vector3") && mxcpp::ValueHolds<Vec3f>(v))
    return v;
  if ((type == "color4" || type == "vector4") && mxcpp::ValueHolds<Vec4f>(v))
    return v;
  return {};
}

// A value as the Vec4f a sampler node gives while its sampler is invalid
Vec4f asVec4(const Value &v)
{
  if (mxcpp::ValueHolds<float>(v))
    return Vec4f(mxcpp::ValueGet<float>(v), 0.f, 0.f, 1.f);
  if (mxcpp::ValueHolds<Vec2f>(v)) {
    const Vec2f &a = mxcpp::ValueGet<Vec2f>(v);
    return Vec4f(a[0], a[1], 0.f, 1.f);
  }
  if (mxcpp::ValueHolds<Vec3f>(v)) {
    const Vec3f &a = mxcpp::ValueGet<Vec3f>(v);
    return Vec4f(a[0], a[1], a[2], 1.f);
  }
  if (mxcpp::ValueHolds<Vec4f>(v))
    return mxcpp::ValueGet<Vec4f>(v);
  return Vec4f(0.f, 0.f, 0.f, 1.f);
}

const char *kindName(MaterialXOverride::Kind k)
{
  switch (k) {
  case MaterialXOverride::Kind::VALUE:
    return "value";
  case MaterialXOverride::Kind::STRING:
    return "string";
  case MaterialXOverride::Kind::SAMPLER:
  default:
    return "sampler";
  }
}

} // namespace

ConvertedMaterial applyMaterialXOverrides(const MaterialXMaterial &material,
    const std::vector<MaterialXOverride> &overrides,
    std::vector<MaterialXDiagnostic> &diagnostics)
{
  GraphEditor editor(material.graph);
  const auto warn = [&](const std::string &message) {
    diagnostics.push_back({Severity::WARNING, message});
  };

  for (const MaterialXOverride &o : overrides) {
    // a texture input, by path
    const auto texture = material.textureInputs.find(o.name);
    if (texture != material.textureInputs.end()) {
      const MaterialXTextureInput &t = texture->second;
      GraphNode &node = editor.graph().nodes[t.node];
      if (o.kind == MaterialXOverride::Kind::STRING) {
        node.parameters[t.input] = Value(resolvePath(o.string, t.directory));
      } else if (o.kind == MaterialXOverride::Kind::SAMPLER) {
        // ANARI samplers aren't colour-managed (ADR 0005)
        node.parameters[t.input] = Value(
            HalcyonTextureSystem::samplerKey(editor.samplerIndex(o.sampler)));
        node.parameters.erase("colorSpace:" + t.input);
      } else {
        warn("texture input '" + o.name
            + "' takes a file name (STRING) or a sampler, not a value");
      }
      continue;
    }

    // a shader input, by name
    const auto input = material.shaderInputs.find(o.name);
    if (input == material.shaderInputs.end() || material.shaderNode.empty()) {
      warn("'" + o.name + "' is neither an input of "
          + (material.shaderNodeDef.empty() ? std::string("the shader")
                                            : material.shaderNodeDef)
          + " nor a texture input of the material; it's ignored");
      continue;
    }
    const std::string &type = input->second.type;
    GraphNode &node = editor.graph().nodes[material.shaderNode];
    const auto set = [&](const Value &v) {
      node.inputConnections.erase(o.name);
      node.parameters.erase("colorSpace:" + o.name);
      node.parameters[o.name] = v;
    };

    if (o.kind == MaterialXOverride::Kind::VALUE
        || (o.kind == MaterialXOverride::Kind::STRING && type == "string")) {
      const Value v = o.kind == MaterialXOverride::Kind::STRING
          ? Value(o.string)
          : valueOfType(o.value, type);
      if (mxcpp::ValueIsEmpty(v)) {
        warn("input '" + o.name + "' is a " + type
            + "; the parameter's type doesn't match, so it's ignored");
        continue;
      }
      set(v);
      continue;
    }

    InputType connectable;
    if (!connectableType(type, connectable)) {
      warn("input '" + o.name + "' is a " + type + ", which a "
          + kindName(o.kind) + " can't set; it's ignored");
      continue;
    }
    // the input's value while a sampler is invalid: as authored, else the
    // nodedef's default
    const auto authored = node.parameters.find(o.name);
    const Vec4f current = asVec4(authored != node.parameters.end()
                && !node.inputConnections.count(o.name)
            ? authored->second
            : input->second.defaultValue);
    MaterialParameter p;
    if (o.kind == MaterialXOverride::Kind::SAMPLER) {
      p = MaterialParameter::fromSampler(o.sampler, current);
    } else if (attributeFromName(o.string) >= 0) {
      p = MaterialParameter::fromAttribute(o.string, current);
    } else {
      warn("input '" + o.name + "': '" + o.string
          + "' names no attribute; it's ignored");
      continue;
    }
    node.parameters.erase("colorSpace:" + o.name);
    editor.connect(material.shaderNode, o.name, p, connectable, o.name);
  }
  return editor.take();
}

} // namespace halcyon::pathtracer
