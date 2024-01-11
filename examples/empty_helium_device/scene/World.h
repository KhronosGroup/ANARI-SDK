// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Instance.h"

namespace hecore {

struct World : public Object
{
  World(HeCoreDeviceGlobalState *s);
  ~World() override;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint32_t flags) override;

  void commit() override;

  const std::vector<Instance *> &instances() const;

 private:
  void cleanup();

  helium::IntrusivePtr<ObjectArray> m_zeroSurfaceData;
  helium::IntrusivePtr<ObjectArray> m_zeroVolumeData;

  helium::IntrusivePtr<ObjectArray> m_instanceData;
  std::vector<Instance *> m_instances;

  bool m_addZeroInstance{false};
  helium::IntrusivePtr<Group> m_zeroGroup;
  helium::IntrusivePtr<Instance> m_zeroInstance;
};

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(hecore::World *, ANARI_WORLD);
