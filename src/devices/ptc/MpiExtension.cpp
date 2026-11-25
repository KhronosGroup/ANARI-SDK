// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// ptc
#include "MpiExtension.h"
// std
#include <algorithm>
#include <cstring>

namespace anari_ptc {

constexpr const char *MPI_COMMUNICATOR = "mpiCommunicator";

ExtensionList::ExtensionList(const char *const *wrapped)
{
  for (auto e = wrapped; e && *e; e++)
    m_names.emplace_back(*e);
  m_names.emplace_back(MPI_EXTENSION_NAME);
  m_names.emplace_back(FRAME_COMPLETION_CALLBACK_EXTENSION_NAME);
  std::sort(m_names.begin(), m_names.end());
  m_names.erase(std::unique(m_names.begin(), m_names.end()), m_names.end());

  for (const auto &n : m_names)
    m_list.push_back(n.c_str());
  m_list.push_back(nullptr);
}

const char **ExtensionList::data()
{
  return m_list.data();
}

bool isMpiCommunicatorParameter(
    ANARIDataType objectType, const char *name, ANARIDataType type)
{
  return objectType == ANARI_FRAME && type == ANARI_VOID_POINTER && name
      && std::strcmp(name, MPI_COMMUNICATOR) == 0;
}

const void *mpiCommunicatorInfo(const char *infoName, ANARIDataType infoType)
{
  static constexpr int32_t notRequired = 0;
  static constexpr const char *description =
      "the MPI communicator which the device should treat as the MPI world"
      " (defaults to MPI_COMM_WORLD)";
  static constexpr const char *sourceExtension = "KHR_DATA_PARALLEL_MPI";

  if (!infoName)
    return nullptr;
  const std::string info = infoName;
  if (info == "required" && infoType == ANARI_BOOL)
    return &notRequired;
  else if (info == "description" && infoType == ANARI_STRING)
    return description;
  else if (info == "sourceExtension" && infoType == ANARI_STRING)
    return sourceExtension;
  return nullptr;
}

const ANARIParameter *FrameParameterLists::withMpiCommunicator(
    const char *subtype, const ANARIParameter *wrapped)
{
  if (!wrapped)
    return nullptr;

  std::lock_guard<std::mutex> lock(m_mutex);
  auto &list = m_lists[subtype ? subtype : ""];
  if (list.empty()) {
    for (auto p = wrapped; p->name; p++) {
      if (!isMpiCommunicatorParameter(ANARI_FRAME, p->name, p->type))
        list.push_back(*p);
    }
    list.push_back({MPI_COMMUNICATOR, ANARI_VOID_POINTER});
    list.push_back({nullptr, ANARI_UNKNOWN});
  }
  return list.data();
}

} // namespace anari_ptc
