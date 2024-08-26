// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/pxr.h>
#include <pxr/base/tf/staticTokens.h>

#define HDANARI_TOKENS \
  (attribute0) \
  (attribute1) \
  (attribute2) \
  (attribute3) \
  (color) \
  ((faceVaryingAttribute0, "faceVarying.attribute0")) \
  ((faceVaryingAttribute1, "faceVarying.attribute1")) \
  ((faceVaryingAttribute2, "faceVarying.attribute2")) \
  ((faceVaryingAttribute3, "faceVarying.attribute3")) \
  ((faceVaryingColor, "faceVarying.color")) \
  ((faceVaryingNormal, "faceVarying.normal")) \
  (id) \
  ((instanceAttribute0, "attribute0")) \
  ((instanceAttribute1, "attribute1")) \
  ((instanceAttribute2, "attribute2")) \
  ((instanceAttribute3, "attribute3")) \
  ((instanceColor, "color")) \
  (normal) \
  (position) \
  ((primitiveAttribute0, "primitive.attribute0")) \
  ((primitiveAttribute1, "primitive.attribute1")) \
  ((primitiveAttribute2, "primitive.attribute2")) \
  ((primitiveAttribute3, "primitive.attribute3")) \
  ((primitiveColor, "primitive.color")) \
  ((primitiveIndex, "primitive.index")) \
  (transform) \
  ((vertexAttribute0, "vertex.attribute0")) \
  ((vertexAttribute1, "vertex.attribute1")) \
  ((vertexAttribute2, "vertex.attribute2")) \
  ((vertexAttribute3, "vertex.attribute3")) \
  ((vertexColor, "vertex.color")) \
  ((vertexNormal, "vertex.normal")) \
  ((vertexPosition, "vertex.position"))

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_PUBLIC_TOKENS(HdAnariTokens, HDANARI_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE
