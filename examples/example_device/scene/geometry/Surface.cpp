// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"

namespace anari {
namespace example_device {

Surface::Surface()
{
  setCommitPriority(COMMIT_PRIORITY_SURFACE);
}

void Surface::commit()
{
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");

  SceneObject::setBounds(m_geometry ? m_geometry->bounds() : box3());
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Surface *);

} // namespace anari
