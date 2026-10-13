// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DelegateRegistry.h"
#include "HydraDevice.h"
#include "anari/backend/LibraryImpl.h"
#include "hydra/Worker.h"

#include "anari_library_hydra_export.h"

// pxr
#include <pxr/base/tf/token.h>
// std
#include <map>
#include <string>
#include <vector>

namespace anari_hydra {

struct HydraLibrary : public anari::LibraryImpl
{
  HydraLibrary(
      void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr);

  ANARIDevice newDevice(const char *subtype) override;
  const char **getDeviceExtensions(const char *deviceType) override;
  const char **getDeviceSubtypes() override;

 private:
  // Per-delegate extension lists (SPEC §4.8), cached by device subtype so the
  // returned C-API pointer stays valid for the library's lifetime.
  std::map<std::string, std::vector<std::string>> m_extStrings;
  std::map<std::string, std::vector<const char *>> m_extView;
};

HydraLibrary::HydraLibrary(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice HydraLibrary::newDevice(const char *subtype)
{
  return (ANARIDevice) new HydraDevice(this_library(), subtype);
}

const char **HydraLibrary::getDeviceExtensions(const char *deviceType)
{
  // Per-delegate truth (SPEC §4.8): probe the delegate's published capabilities
  // and gate the reported extension list on them, honestly omitting what the
  // delegate cannot do. Cached per subtype.
  const std::string sub = deviceType ? deviceType : "default";
  auto it = m_extView.find(sub);
  if (it != m_extView.end())
    return it->second.data();

  const pxr::TfToken plugin = DelegateRegistry::get().pluginForSubtype(sub);
  pxr::TfTokenVector rprims, sprims, bprims;
  std::set<std::string> aovChannels;
  std::vector<RenderSettingDesc> renderSettings; // unused at library scope
  {
    // The delegate is created and read on a worker thread that owns an Hgi
    // (SPEC §9.2); a short-lived worker is enough for a one-shot probe.
    Worker worker;
    worker.postWait([&]() {
      readDelegateCaps(
          plugin, rprims, sprims, bprims, aovChannels, renderSettings);
    });
  }
  buildExtensionList(
      rprims, sprims, bprims, aovChannels, m_extStrings[sub], m_extView[sub]);
  return m_extView[sub].data();
}

const char **HydraLibrary::getDeviceSubtypes()
{
  // One device subtype per Hydra render delegate (SPEC §11.2).
  return DelegateRegistry::get().subtypeCStrings();
}

} // namespace anari_hydra

// Define library entrypoint //////////////////////////////////////////////////

extern "C" HYDRA_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    hydra, handle, scb, scbPtr)
{
  return (ANARILibrary) new anari_hydra::HydraLibrary(handle, scb, scbPtr);
}
