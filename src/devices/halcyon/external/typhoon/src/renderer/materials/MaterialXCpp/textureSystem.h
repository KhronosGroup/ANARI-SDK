//
// MaterialXCpp texture system abstraction.
//
#ifndef MXCPP_TEXTURE_SYSTEM_H
#define MXCPP_TEXTURE_SYSTEM_H

#include "mathTypes.h"

#include <string>

namespace mxcpp {

enum class TextureAddressMode {
    Constant,
    Clamp,
    Periodic,
    Mirror,
    UseMetadata
};

enum class TextureFilterType {
    Closest,
    Linear,
    Cubic,
    SmartBicubic
};

enum class TextureDataRole {
    Color,
    NonColor
};

enum class TextureSampleStatus {
    Ok,
    Missing,
    Error
};

struct Texture2DRequest
{
    std::string filePath;
    std::string layerName;

    Vec2f st = Vec2f(0.0f);
    Vec2f dstdx = Vec2f(0.0f);
    Vec2f dstdy = Vec2f(0.0f);
    Vec2f blur = Vec2f(0.0f);

    TextureAddressMode uAddressMode = TextureAddressMode::Periodic;
    TextureAddressMode vAddressMode = TextureAddressMode::Periodic;
    TextureFilterType filterType = TextureFilterType::Linear;

    // The node owns frame-sequence semantics, but the request keeps these
    // fields so backends can participate if needed later.
    std::string frameRange;
    int frameOffset = 0;
    TextureAddressMode frameEndAction = TextureAddressMode::Constant;
    float frame = 0.0f;

    TextureDataRole dataRole = TextureDataRole::NonColor;
    std::string sourceColorSpace;
    // Only native UsdUVTexture's auto/default policy requests file inspection.
    bool inferSrgbFromFile = false;

    int channelCount = 4;
    float channelFillValue = 0.0f;
    Vec4f defaultValue = Vec4f(0.0f);
};

struct Texture2DResult
{
    Vec4f value = Vec4f(0.0f);
    TextureSampleStatus status = TextureSampleStatus::Missing;
};

class TextureSystem
{
public:
    virtual ~TextureSystem() = default;

    /// Sample one texture lookup. Implementations must translate recoverable
    /// backend failures into Missing or Error results and must not let those
    /// failures escape graph evaluation as exceptions.
    virtual Texture2DResult Sample2D(
        const Texture2DRequest& request) const = 0;
};

}  // namespace mxcpp

#endif
