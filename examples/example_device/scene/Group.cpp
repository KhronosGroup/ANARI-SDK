// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"

namespace anari {
namespace example_device {

Group::Group()
{
  setCommitPriority(COMMIT_PRIORITY_GROUP);
}

void Group::commit()
{
  m_surfaceData = getParamObject<ObjectArray>("surface");
  m_volumeData = getParamObject<ObjectArray>("volume");

  m_surfaces.reset();
  m_volumes.reset();

  if (m_surfaceData) {
    m_surfaces =
        make_Span((Surface **)m_surfaceData->handles(), m_surfaceData->size());
  }

  if (m_volumeData) {
    m_volumes = make_Span(
        (Volume **)m_volumeData->handles(), m_volumeData->size());
  }

  auto b = box3();

  auto surfaceBounds = buildBVH(m_surfaceBvh, m_surfaces);
  auto volumeBounds = buildBVH(m_volumeBvh, m_volumes);

  b.extend(surfaceBounds);
  b.extend(volumeBounds);

  SceneObject::setBounds(b);
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Group *);

} // namespace anari
