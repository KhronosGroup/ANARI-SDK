//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "instancer.h"

#include "debugCodes.h"
#include "sampler.h"

#include <pxr/base/gf/declare.h>
#include <pxr/base/tf/debug.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/quaternion.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE


HdAnariInstancer::HdAnariInstancer(HdSceneDelegate* delegate,
                                     SdfPath const& id)
    : HdInstancer(delegate, id)
{
  TF_DEBUG_MSG(HD_ANARI_INSTANCER, "Creating instancer with id %s\n", id.GetText());
}

HdAnariInstancer::~HdAnariInstancer()
{
    TF_FOR_ALL(it, _primvarMap) {
        delete it->second;
    }
    _primvarMap.clear();
}

void
HdAnariInstancer::Sync(HdSceneDelegate* delegate,
                        HdRenderParam* renderParam,
                        HdDirtyBits* dirtyBits)
{
    TF_DEBUG_MSG(HD_ANARI_INSTANCER, "Syncing instancer at %s\n", GetId().GetText());
    _UpdateInstancer(delegate, dirtyBits);

    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, GetId())) {
        _SyncPrimvars(delegate, *dirtyBits);
    }
}

void
HdAnariInstancer::_SyncPrimvars(HdSceneDelegate* delegate,
                                 HdDirtyBits dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();

    HdPrimvarDescriptorVector primvars =
        delegate->GetPrimvarDescriptors(id, HdInterpolationInstance);

    for (HdPrimvarDescriptor const& pv: primvars) {
        TF_DEBUG_MSG(HD_ANARI_INSTANCER, "Parsing primvar %s for path %s\n",
          pv.name.GetText(), id.GetText());
        if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name)) {
            VtValue value = delegate->Get(id, pv.name);
            if (!value.IsEmpty()) {
              TF_DEBUG_MSG(HD_ANARI_INSTANCER,
                "    primvar %s has value of type %s\n", pv.name.GetText(), value.GetTypeName().c_str());
                if (_primvarMap.count(pv.name) > 0) {
                    delete _primvarMap[pv.name];
                }
                _primvarMap[pv.name] =
                    new HdVtBufferSource(pv.name, value);
                
            }
        }
    }
}

VtMatrix4dArray
HdAnariInstancer::ComputeInstanceTransforms(SdfPath const &prototypeId)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    // The transforms for this level of instancer are computed by:
    // foreach(index : indices) {
    //     instancerTransform
    //     * hydra:instanceTranslations(index)
    //     * hydra:instanceRotations(index)
    //     * hydra:instanceScales(index)
    //     * hydra:instanceTransforms(index)
    // }
    // If any transform isn't provided, it's assumed to be the identity.

    GfMatrix4d instancerTransform =
        GetDelegate()->GetInstancerTransform(GetId());
    VtIntArray instanceIndices =
        GetDelegate()->GetInstanceIndices(GetId(), prototypeId);

    VtMatrix4dArray transforms(instanceIndices.size());
    for (size_t i = 0; i < instanceIndices.size(); ++i) {
        transforms[i] = instancerTransform;
    }

    // "hydra:instanceTranslations" holds a translation vector for each index.
    if (_primvarMap.count(HdInstancerTokens->instanceTranslations) > 0) {
        HdAnariBufferSampler
                sampler(*_primvarMap[HdInstancerTokens->instanceTranslations]);
        for (size_t i = 0; i < instanceIndices.size(); ++i) {
            GfVec3f translate;
            if (sampler.Sample(instanceIndices[i], &translate)) {
                GfMatrix4d translateMat(1);
                translateMat.SetTranslate(GfVec3d(translate));
                transforms[i] = translateMat * transforms[i];
            }
        }
    }

    // "hydra:instanceRotations" holds a quaternion in <real, i, j, k>
    // format for each index.
    if (_primvarMap.count(HdInstancerTokens->instanceRotations) > 0) {
        TF_DEBUG_MSG(HD_ANARI_INSTANCER, "Parsing rotation for path %s\n", prototypeId.GetText());
        HdAnariBufferSampler sampler(*_primvarMap[HdInstancerTokens->instanceRotations]);
        for (size_t i = 0; i < instanceIndices.size(); ++i) {
            GfVec4f quat;
            if (sampler.Sample(instanceIndices[i], &quat)) {
                GfMatrix4d rotateMat(1);
                rotateMat.SetRotate(GfQuatd(
                    quat[0], quat[1], quat[2], quat[3]));
                transforms[i] = rotateMat * transforms[i];
            }
            GfVec4f quatd;
            if (sampler.Sample(instanceIndices[i], &quatd)) {
                GfMatrix4d rotateMat(1);
                rotateMat.SetRotate(GfQuatd(
                    quatd[0], quatd[1], quatd[2], quatd[3]));
                transforms[i] = rotateMat * transforms[i];
            }
            GfQuath quath;
            if (sampler.Sample(instanceIndices[i], &quath)) {
                GfMatrix4d rotateMat(1);
                rotateMat.SetRotate(GfQuatd(quath));
                transforms[i] = rotateMat * transforms[i];
            }
        }
    }

    // "hydra:instanceScales" holds an axis-aligned scale vector for each index.
    if (_primvarMap.count(HdInstancerTokens->instanceScales) > 0) {
        HdAnariBufferSampler sampler(*_primvarMap[HdInstancerTokens->instanceScales]);
        for (size_t i = 0; i < instanceIndices.size(); ++i) {
            GfVec3f scale;
            if (sampler.Sample(instanceIndices[i], &scale)) {
                GfMatrix4d scaleMat(1);
                scaleMat.SetScale(GfVec3d(scale));
                transforms[i] = scaleMat * transforms[i];
            }
        }
    }

    // "hydra:instanceTransforms" holds a 4x4 transform matrix for each index.
    if (_primvarMap.count(HdInstancerTokens->instanceTransforms) > 0) {
        HdAnariBufferSampler
                sampler(*_primvarMap[HdInstancerTokens->instanceTransforms]);
        for (size_t i = 0; i < instanceIndices.size(); ++i) {
            GfMatrix4d instanceTransform;
            if (sampler.Sample(instanceIndices[i], &instanceTransform)) {
                transforms[i] = instanceTransform * transforms[i];
            }
        }
    }

    if (GetParentId().IsEmpty()) {
        return transforms;
    }

    HdInstancer *parentInstancer =
        GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
    if (!TF_VERIFY(parentInstancer)) {
        return transforms;
    }

    // The transforms taking nesting into account are computed by:
    // parentTransforms = parentInstancer->ComputeInstanceTransforms(GetId())
    // foreach (parentXf : parentTransforms, xf : transforms) {
    //     parentXf * xf
    // }
    VtMatrix4dArray parentTransforms =
        static_cast<HdAnariInstancer*>(parentInstancer)->
            ComputeInstanceTransforms(GetId());

    VtMatrix4dArray final(parentTransforms.size() * transforms.size());
    for (size_t i = 0; i < parentTransforms.size(); ++i) {
        for (size_t j = 0; j < transforms.size(); ++j) {
            final[i * transforms.size() + j] = transforms[j] *
                                               parentTransforms[i];
        }
    }
    return final;
}

PXR_NAMESPACE_CLOSE_SCOPE

