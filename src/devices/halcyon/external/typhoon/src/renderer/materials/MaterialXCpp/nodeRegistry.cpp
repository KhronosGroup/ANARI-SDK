//
// MaterialXCpp node registry — pxr-independent.
//
#include "nodeRegistry.h"
#include "surfaceShaderUtils.h"

#include <renderer/materials/MaterialXCpp/nodes/adjustmentNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/applicationNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/channelNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/colorTransformNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/compositingNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/conditionalNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/convolutionNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/geometricNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/mathNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/nprNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/pbrNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/procedural2dNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/procedural3dNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/proceduralNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/texture3dNodes.h>
#include <renderer/materials/MaterialXCpp/nodes/textureNodes.h>

#include <mutex>

namespace mxcpp {

namespace {

const SlotName _color("color");
const SlotName _bg("bg");
const SlotName _fg("fg");
const SlotName _mix("mix");
const SlotName _out("out");

void
_EvalUniformEdf(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    // MaterialX's uniform_edf is direction-independent, so its closure can
    // be represented by emitted radiance, but keep it typed as an EDF closure
    // until ND_surface consumes it.
    (*outputs)[_out] = Value(
        UniformEdf{Get<Vec3f>(inputs, _color, Vec3f(1.0f))});
}

void
_EvalSurface(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    (*outputs)[_out] = Value(EvalSurfaceConstructor(inputs));
}

void
_EvalSurfaceUnlit(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    (*outputs)[_out] = Value(EvalSurfaceUnlit(inputs));
}

void
_EvalVolume(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    (*outputs)[_out] = Value(EvalVolumeConstructor(inputs));
}

void
_EvalMixSurfaceShader(
    const ParamMap& inputs,
    const ShadingContext&,
    NodeOutputMap* outputs)
{
    const SurfaceClosure empty = MakeEmptySurfaceClosure();
    (*outputs)[_out] = Value(MixSurfaceClosures(
        Get<SurfaceClosure>(inputs, _bg, empty),
        Get<SurfaceClosure>(inputs, _fg, empty),
        Get<float>(inputs, _mix, 0.0f)));
}

} // namespace

NodeRegistry&
NodeRegistry::GetInstance()
{
    static NodeRegistry instance;
    return instance;
}

void
NodeRegistry::Register(const std::string& nodeTypeId, NodeEvalFn fn)
{
    _nodes[nodeTypeId] = fn;
}

NodeEvalFn
NodeRegistry::Find(const std::string& nodeTypeId) const
{
    auto it = _nodes.find(nodeTypeId);
    if (it != _nodes.end()) {
        return it->second;
    }
    return nullptr;
}

/* static */
void
NodeRegistry::RegisterBuiltinNodes()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        auto& reg = GetInstance();
        RegisterMathNodes(reg);
        RegisterConvolutionNodes(reg);
        RegisterNprNodes(reg);
        RegisterAdjustmentNodes(reg);
        RegisterChannelNodes(reg);
        RegisterConditionalNodes(reg);
        RegisterCompositingNodes(reg);
        RegisterGeometricNodes(reg);
        RegisterApplicationNodes(reg);
        RegisterTextureNodes(reg);
        RegisterTexture3dNodes(reg);
        RegisterProceduralNodes(reg);
        RegisterProcedural2dNodes(reg);
        RegisterProcedural3dNodes(reg);
        RegisterPbrNodes(reg);
        RegisterColorTransformNodes(reg);
        reg.Register("ND_uniform_edf", &_EvalUniformEdf);
        reg.Register("ND_surface", &_EvalSurface);
        reg.Register("ND_surface_unlit", &_EvalSurfaceUnlit);
        reg.Register("ND_volume", &_EvalVolume);
        reg.Register("ND_mix_surfaceshader", &_EvalMixSurfaceShader);
    });
}

}  // namespace mxcpp
