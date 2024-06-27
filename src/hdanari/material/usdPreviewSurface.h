#pragma once

#include <anari/anari_cpp.hpp>
#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariUsdPreviewSurfaceConverter {
public:
    enum class AlphaMode {
        Opaque,
        Blend,
        Mask,
    };

    HdAnariUsdPreviewSurfaceConverter(anari::Device device, anari::Material material)
        : _device(device), _material(material) {}

    void ProcessInputConnection(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName, TfToken usdInputName, const char* anariParameterName) {
        if (!ProcessConnection(materialNetworkIface, usdPreviewSurfaceName, usdInputName, anariParameterName)) {
            anari::unsetParameter(_device, _material, anariParameterName);
        }
    }

    template<typename T>
    void ProcessInputValue(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName, TfToken usdInputName, const char* anariParameterName, T defaultValue) {
        if (ProcessValue(materialNetworkIface, usdPreviewSurfaceName, usdInputName, anariParameterName)) return;

        if constexpr (std::is_void<T>()) {
            anari::unsetParameter(_device, _material, anariParameterName);
        } else {
            anari::setParameter(_device, _material, anariParameterName, defaultValue);
        }
    }

    template<typename T>
    void ProcessInput(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName, TfToken usdInputName, const char* anariParameterName, T defaultValue) {
        if (ProcessConnection(materialNetworkIface, usdPreviewSurfaceName, usdInputName, anariParameterName)) return;
        if (ProcessValue(materialNetworkIface, usdPreviewSurfaceName, usdInputName, anariParameterName)) return;

        if constexpr (std::is_void<T>()) {
            anari::unsetParameter(_device, _material, anariParameterName);
        } else {
            anari::setParameter(_device, _material, anariParameterName, defaultValue);
        }
    }

    bool UseSpecularWorkflow(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName) const;
    AlphaMode GetAlphaMode(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName) const;

private:
    anari::Device _device;
    anari::Material _material;

    bool ProcessConnection(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName, TfToken usdInputName, const char* anariParameterName);
    bool ProcessValue(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName, TfToken usdInputName, const char* anariParameterName);

    void ProcessUSDUVTexture(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdTextureName, TfToken outputName, const char* anariParameterName);

    void ProcessUsdPrimvarReader(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName, TfToken usdInputName,
        const char* anariParameterName, ANARIDataType anariDataType);
};

PXR_NAMESPACE_CLOSE_SCOPE
