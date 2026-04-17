#version 450

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 FragBlurred;

// Fragment samplers (set=2)
layout(set = 2, binding = 0) uniform sampler2D aoTex;
layout(set = 2, binding = 1) uniform sampler2D depthTex;
layout(set = 2, binding = 2) uniform usampler2D idsTex;

// Blur parameters (set=3, binding=0)
layout(set = 3, binding = 0) uniform BlurParams {
    vec2 texelSize;
    int blurRadius;
} u;

void main()
{
    float centerDepth = texture(depthTex, vUV).r;
    uvec4 centerIds = texture(idsTex, vUV);

    float result = 0.0;
    float totalWeight = 0.0;

    for (int x = -u.blurRadius; x <= u.blurRadius; ++x) {
        for (int y = -u.blurRadius; y <= u.blurRadius; ++y) {
            vec2 offset = vec2(float(x), float(y)) * u.texelSize;
            vec2 sampleUV = vUV + offset;

            float sampleDepth = texture(depthTex, sampleUV).r;
            float sampleAO = texture(aoTex, sampleUV).r;
            uvec4 sampleIds = texture(idsTex, sampleUV);

            // Skip neighbors on different objects
            if (sampleIds.g != centerIds.g || sampleIds.b != centerIds.b) {
                continue;
            }

            // Bilateral weight: suppress blurring across depth edges
            float depthDiff = abs(centerDepth - sampleDepth);
            float w = exp(-depthDiff * 1000.0);

            result += sampleAO * w;
            totalWeight += w;
        }
    }

    float ao = result / max(totalWeight, 0.001);
    FragBlurred = vec4(ao, ao, ao, 1.0);
}
