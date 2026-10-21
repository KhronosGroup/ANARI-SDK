// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_objects.isph"

// World-space light records (struct Light), which the world keeps in an array
// of its own (World::lights): the light subtypes (Light.cpp) write one per
// instance transform through these setters

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_Light()
{
  return sizeof(uniform Light);
}

static inline uniform float3 toFloat3(const uniform float *uniform v)
{
  return make_float3(v[0], v[1], v[2]);
}

// Clears every field but the ones all lights have
static void Light_init(uniform Light *uniform self,
    uniform LightType type,
    const uniform float *uniform emission /* float3 */,
    uniform bool visible)
{
  self->type = type;
  self->axis = make_float3(0.f);
  self->position = make_float3(0.f);
  self->edge1 = make_float3(0.f);
  self->edge2 = make_float3(0.f);
  self->frameX = make_float3(0.f);
  self->frameY = make_float3(0.f);
  self->c0 = make_float3(0.f);
  self->c90 = make_float3(0.f);
  self->halfAngle = 0.f;
  self->radius = 0.f;
  self->innerRadiusFraction2 = 0.f;
  self->area = 0.f;
  self->coneAngle = PI;
  self->coneSoftAngle = PI;
  self->front = false;
  self->back = false;
  self->distribution = NULL;
  self->numGamma = 0;
  self->numC = 0;
  self->texels = NULL;
  self->texelWeights = NULL;
  self->conditionalCdf = NULL;
  self->marginalCdf = NULL;
  self->weightSum = 0.f;
  self->width = 0;
  self->height = 0;
  self->emission = toFloat3(emission);
  self->visible = visible;
}

static void Light_setDistribution(uniform Light *uniform self,
    const uniform float *uniform c0 /* float3 */,
    const uniform float *uniform c90 /* float3 */,
    const uniform float *uniform distribution,
    uniform uint32 numGamma,
    uniform uint32 numC)
{
  self->c0 = toFloat3(c0);
  self->c90 = toFloat3(c90);
  self->distribution = numGamma > 1 && numC > 0 ? distribution : NULL;
  self->numGamma = self->distribution ? numGamma : 0;
  self->numC = self->distribution ? numC : 0;
}

export void Light_setDirectional(void *uniform _self,
    const uniform float *uniform axis /* float3 */,
    uniform float halfAngle,
    const uniform float *uniform emission /* float3 */,
    uniform bool visible)
{
  uniform Light *uniform self = (uniform Light * uniform) _self;
  Light_init(self, LIGHT_DIRECTIONAL, emission, visible);
  self->axis = toFloat3(axis);
  self->halfAngle = halfAngle;
}

export void Light_setPoint(void *uniform _self,
    const uniform float *uniform position /* float3 */,
    uniform float radius,
    const uniform float *uniform emission /* float3 */,
    uniform bool visible)
{
  uniform Light *uniform self = (uniform Light * uniform) _self;
  Light_init(self, LIGHT_POINT, emission, visible);
  self->position = toFloat3(position);
  self->radius = radius;
}

export void Light_setSpot(void *uniform _self,
    const uniform float *uniform position /* float3 */,
    const uniform float *uniform axis /* float3 */,
    uniform float coneAngle,
    uniform float coneSoftAngle,
    const uniform float *uniform emission /* float3 */,
    uniform bool visible)
{
  uniform Light *uniform self = (uniform Light * uniform) _self;
  Light_init(self, LIGHT_SPOT, emission, visible);
  self->position = toFloat3(position);
  self->axis = toFloat3(axis);
  self->coneAngle = coneAngle;
  self->coneSoftAngle = coneSoftAngle;
}

export void Light_setQuad(void *uniform _self,
    const uniform float *uniform position /* float3 */,
    const uniform float *uniform edge1 /* float3 */,
    const uniform float *uniform edge2 /* float3 */,
    const uniform float *uniform axis /* float3 */,
    uniform float area,
    uniform bool front,
    uniform bool back,
    const uniform float *uniform c0 /* float3 */,
    const uniform float *uniform c90 /* float3 */,
    const uniform float *uniform distribution,
    uniform uint32 numGamma,
    uniform uint32 numC,
    const uniform float *uniform emission /* float3 */,
    uniform bool visible)
{
  uniform Light *uniform self = (uniform Light * uniform) _self;
  Light_init(self, LIGHT_QUAD, emission, visible);
  self->position = toFloat3(position);
  self->edge1 = toFloat3(edge1);
  self->edge2 = toFloat3(edge2);
  self->axis = toFloat3(axis);
  self->area = area;
  self->front = front;
  self->back = back;
  Light_setDistribution(self, c0, c90, distribution, numGamma, numC);
}

export void Light_setRing(void *uniform _self,
    const uniform float *uniform position /* float3 */,
    const uniform float *uniform axis /* float3 */,
    const uniform float *uniform frameX /* float3 */,
    const uniform float *uniform frameY /* float3 */,
    uniform float radius,
    uniform float innerRadiusFraction2,
    uniform float area,
    uniform float coneAngle,
    uniform float coneSoftAngle,
    const uniform float *uniform c0 /* float3 */,
    const uniform float *uniform c90 /* float3 */,
    const uniform float *uniform distribution,
    uniform uint32 numGamma,
    uniform uint32 numC,
    const uniform float *uniform emission /* float3 */,
    uniform bool visible)
{
  uniform Light *uniform self = (uniform Light * uniform) _self;
  Light_init(self, LIGHT_RING, emission, visible);
  self->position = toFloat3(position);
  self->axis = toFloat3(axis);
  self->frameX = toFloat3(frameX);
  self->frameY = toFloat3(frameY);
  self->radius = radius;
  self->innerRadiusFraction2 = innerRadiusFraction2;
  self->area = area;
  self->coneAngle = coneAngle;
  self->coneSoftAngle = coneSoftAngle;
  Light_setDistribution(self, c0, c90, distribution, numGamma, numC);
}

export void Light_setHdri(void *uniform _self,
    const uniform float *uniform frameX /* float3 */,
    const uniform float *uniform frameY /* float3 */,
    const uniform float *uniform axis /* float3 */,
    const uniform float *uniform texels,
    uniform uint32 width,
    uniform uint32 height,
    const uniform float *uniform texelWeights,
    const uniform float *uniform conditionalCdf,
    const uniform float *uniform marginalCdf,
    uniform float weightSum,
    const uniform float *uniform emission /* float3 */,
    uniform bool visible)
{
  uniform Light *uniform self = (uniform Light * uniform) _self;
  Light_init(self, LIGHT_HDRI, emission, visible);
  self->frameX = toFloat3(frameX);
  self->frameY = toFloat3(frameY);
  self->axis = toFloat3(axis);
  self->texels = texels;
  self->width = width;
  self->height = height;
  const uniform bool distribution = texelWeights && conditionalCdf
      && marginalCdf && weightSum > 0.f;
  self->texelWeights = distribution ? texelWeights : NULL;
  self->conditionalCdf = distribution ? conditionalCdf : NULL;
  self->marginalCdf = distribution ? marginalCdf : NULL;
  self->weightSum = distribution ? weightSum : 0.f;
}

// The white texel of the ambient light's map
static const uniform float AMBIENT_TEXEL[3] = {1.f, 1.f, 1.f};

// The 'quality''s ambient light ('ambientColor' x 'ambientRadiance',
// 'emission'): Typhoon's dome light without a texture, an hdri of one white
// texel without a sampling distribution (so sampled uniformly over the
// sphere) in the world frame (a dome without a transform), that camera rays
// don't see
export void Light_setAmbient(
    void *uniform _self, const uniform float *uniform emission /* float3 */)
{
  const uniform float frameX[3] = {1.f, 0.f, 0.f};
  const uniform float frameY[3] = {0.f, 1.f, 0.f};
  const uniform float axis[3] = {0.f, 0.f, 1.f};
  Light_setHdri(_self,
      frameX,
      frameY,
      axis,
      AMBIENT_TEXEL,
      1,
      1,
      NULL,
      NULL,
      NULL,
      0.f,
      emission,
      false);
}
