// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HalcyonDeviceGlobalState.h"

namespace halcyon {

HalcyonDeviceGlobalState::HalcyonDeviceGlobalState(ANARIDevice d)
    : helium::BaseGlobalDeviceState(d)
{}

void HalcyonDeviceGlobalState::commitDeviceParameters(const DeviceParameters &p)
{
  std::lock_guard<std::mutex> lock(m_deviceParametersMutex);
  m_committedDeviceParameters = p;
  m_deviceParametersCommitted = true;
}

void HalcyonDeviceGlobalState::flushCommitBuffer()
{
  // bounded, in case observers notify each other in a cycle
  for (int i = 0; i < 8 && !commitBuffer.empty(); i++)
    commitBuffer.flush();
}

void HalcyonDeviceGlobalState::applyDeviceParameters()
{
  DeviceParameters p;
  {
    std::lock_guard<std::mutex> lock(m_deviceParametersMutex);
    if (!m_deviceParametersCommitted)
      return;
    p = m_committedDeviceParameters;
    m_deviceParametersCommitted = false;
  }

  // surface validity depends on the flag, and groups filter on it
  if (p.allowInvalidSurfaceMaterials != allowInvalidSurfaceMaterials)
    objectUpdates.lastBLSReconstructSceneRequest = helium::newTimeStamp();

  allowInvalidSurfaceMaterials = p.allowInvalidSurfaceMaterials;
  invalidMaterialColor = p.invalidMaterialColor;
  // every commit counts as a change, even one that sets the same values
  lastDeviceParametersApplied = helium::newTimeStamp();
}

} // namespace halcyon