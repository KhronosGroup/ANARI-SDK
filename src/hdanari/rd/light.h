#ifndef HDANARI_RD_LIGHT_H
#define HDANARI_RD_LIGHT_H

#include "anari/anari_cpp.hpp"

#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/gf/matrix4d.h>
#include <anari/anari_cpp.hpp>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariLight : public HdLight {
public:
    HF_MALLOC_TAG_NEW("new HdAnariLight");

    HdAnariLight(anari::Device device, SdfPath const& id, TfToken const& lightType);
    ~HdAnariLight() override;

    void Sync(HdSceneDelegate* sceneDelegate,
                      HdRenderParam* renderParam,
                      HdDirtyBits* dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    anari::Instance GetAnariLightInstance() const { return instance_; }

    void Finalize(HdRenderParam *renderParam) override;


private:
    bool registered_{false};
    TfToken lightType_{};
    anari::Device device_{};
    anari::Instance instance_{};
    anari::Light light_{};
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDANARI_RD_LIGHT_H