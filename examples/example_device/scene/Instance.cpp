// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"

namespace anari {
namespace example_device {

Instance::Instance()
{
  setCommitPriority(COMMIT_PRIORITY_INSTANCE);
}

void Instance::commit()
{
  m_group = getParamObject<Group>("group");

  auto b = m_group ? m_group->bounds() : box3();

  m_xfm.reset();
  m_invXfm.reset();
  m_normXfm.reset();

  if (hasParam("transform")) {
    mat4 xfm = getParam<mat4>("transform", mat4(1));
    b = xfmBox(xfm, b);
    m_xfm = xfm;
    m_invXfm = glm::inverse(xfm);
    m_normXfm = glm::transpose(glm::inverse(mat3(xfm)));
  }

  SceneObject::setBounds(b);
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Instance *);

} // namespace anari
