// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// What ptc adds on top of the wrapped device to report KHR_DATA_PARALLEL_MPI:
// the extension name in extension lists, and query info for the extension's
// one parameter (ANARI_FRAME 'mpiCommunicator').

// anari
#include <anari/anari.h>
// std
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace anari_ptc {

// KHR_DATA_PARALLEL_MPI as spelled in device extension lists (the spelling
// anari_extension_utility.h matches)
constexpr const char *MPI_EXTENSION_NAME = "ANARI_KHR_DATA_PARALLEL_MPI";

// KHR_FRAME_COMPLETION_CALLBACK, which ptc implements itself instead of
// forwarding it to the wrapped device
constexpr const char *FRAME_COMPLETION_CALLBACK_EXTENSION_NAME =
    "ANARI_KHR_FRAME_COMPLETION_CALLBACK";

// A wrapped extension list plus the extensions ptc implements itself
// (KHR_DATA_PARALLEL_MPI, KHR_FRAME_COMPLETION_CALLBACK), without duplicates,
// whatever the wrapped list holds. Strings are copied, so the source list may
// go away afterwards.
struct ExtensionList
{
  explicit ExtensionList(const char *const *wrapped);

  // Null-terminated list, valid for the lifetime of this object
  const char **data();

 private:
  std::vector<std::string> m_names;
  std::vector<const char *> m_list;
};

// Whether this is the (object, parameter) KHR_DATA_PARALLEL_MPI defines
bool isMpiCommunicatorParameter(
    ANARIDataType objectType, const char *name, ANARIDataType type);

// anariGetParameterInfo() result for ANARI_FRAME 'mpiCommunicator'
const void *mpiCommunicatorInfo(const char *infoName, ANARIDataType infoType);

// A wrapped device's frame parameter lists (anariGetObjectInfo 'parameter')
// with 'mpiCommunicator' added, cached per frame subtype. Parameter names
// point into the wrapped device's storage, so a list is valid only while the
// wrapped device lives.
struct FrameParameterLists
{
  // nullptr if 'wrapped' is nullptr (unknown subtype)
  const ANARIParameter *withMpiCommunicator(
      const char *subtype, const ANARIParameter *wrapped);

 private:
  std::mutex m_mutex;
  std::map<std::string, std::vector<ANARIParameter>> m_lists;
};

} // namespace anari_ptc
