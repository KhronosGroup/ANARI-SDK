// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/base/tf/hashmap.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <functional>
#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

/// \class HdAnariInstancer
///
/// HdAnari implements instancing by adding prototype geometry to the BVH
/// multiple times within HdAnariMesh::Sync(). The only instance-varying
/// attribute that HdAnari supports is transform, so the natural
/// accessor to instancer data is ComputeInstanceTransforms(),
/// which returns a list of transforms to apply to the given prototype
/// (one instance per transform).
///
/// Nested instancing can be handled by recursion, and by taking the
/// cartesian product of the transform arrays at each nesting level, to
/// create a flattened transform array.
///
class HdAnariInstancer : public HdInstancer
{
 public:
  /// Constructor.
  ///   \param delegate The scene delegate backing this instancer's data.
  ///   \param id The unique id of this instancer.
  HdAnariInstancer(HdSceneDelegate *delegate, SdfPath const &id);

  /// Destructor.
  ~HdAnariInstancer();

  /// Computes all instance transforms for the provided prototype id,
  /// taking into account the scene delegate's instancerTransform and the
  /// instance primvars "hydra:instanceTransforms",
  /// "hydra:instanceTranslations", "hydra:instanceRotations", and
  /// "hydra:instanceScales". Computes and flattens nested transforms,
  /// if necessary.
  ///   \param prototypeId The prototype to compute transforms for.
  ///   \return One transform per instance, to apply when drawing.
  VtMatrix4dArray ComputeInstanceTransforms(SdfPath const &prototypeId);

  /// Updates cached primvar data from the scene delegate.
  ///   \param sceneDelegate The scene delegate for this prim.
  ///   \param renderParam The hdAnari render param.
  ///   \param dirtyBits The dirty bits for this instancer.
  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits) override;

  TfTokenVector GetPrimvarNames() const;

  // Recursively gather primvar values.
  VtValue GatherInstancePrimvar(
      const SdfPath &prototypeId, const TfToken &primvarName) const;

 private:
  // Updates the cached primvars in _primvarMap based on scene delegate
  // data.  This is a helper function for Sync().
  void _SyncPrimvars(HdSceneDelegate *delegate, HdDirtyBits dirtyBits);

  // Map of the latest primvar data for this instancer, keyed by
  // primvar name. Primvar values are VtValue, an any-type; they are
  // interpreted at consumption time (here, in ComputeInstanceTransforms).
  TfHashMap<TfToken, HdVtBufferSource *, TfToken::HashFunctor> _primvarMap;

  // Recursively gather primvar values.
  template <typename T>
  VtValue GatherInstancePrimvar(const SdfPath &prototypeId,
      const TfToken &primvarName,
      HdType dataType) const;
};

PXR_NAMESPACE_CLOSE_SCOPE
