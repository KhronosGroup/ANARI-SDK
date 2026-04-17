#version 450

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 FragAO;

// Fragment samplers (set=2)
layout(set = 2, binding = 0) uniform sampler2D depthTex;
layout(set = 2, binding = 1) uniform sampler2D normalTex;
layout(set = 2, binding = 2) uniform sampler2D noiseTex;
layout(set = 2, binding = 3) uniform usampler2D idsTex;

// SSAO parameters (set=3, binding=0)
layout(set = 3, binding = 0) uniform SSAOParams {
    mat4 projection;
    mat4 invProjection;
    mat4 view;
    vec4 samples[64];
    float radius;
    float bias;
    float intensity;
    int kernelSize;
    vec2 noiseScale;
} u;

vec3 reconstructViewPos(vec2 uv)
{
    float d = texture(depthTex, uv).r;
    // NDC: UV [0,1] -> [-1,1], with Y negated to undo the geometry pass Y-flip
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    vec4 clipPos = vec4(ndc, d, 1.0);
    vec4 viewPos = u.invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main()
{
    float depth = texture(depthTex, vUV).r;

    // Skip background fragments (depth at far plane)
    if (depth >= 1.0) {
        FragAO = vec4(1.0);
        return;
    }

    vec3 fragPos = reconstructViewPos(vUV);
    vec3 worldNormal = texture(normalTex, vUV).xyz;

    // Transform world-space normal to view-space
    vec3 normal = normalize(mat3(u.view) * worldNormal);

    // Center pixel object+instance IDs for boundary detection
    uvec4 centerIds = texture(idsTex, vUV);

    // Random rotation vector from tiled noise texture
    vec3 randomVec = texture(noiseTex, vUV * u.noiseScale).xyz;

    // Build TBN (tangent-bitangent-normal) matrix
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // Accumulate occlusion
    float occlusion = 0.0;
    int validSamples = 0;

    for (int i = 0; i < u.kernelSize; ++i) {
        // Transform sample from tangent space to view space
        vec3 samplePos = fragPos + TBN * u.samples[i].xyz * u.radius;

        // Project sample to screen space
        vec4 offset = u.projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        // Undo Y-flip to get UV
        offset.y = -offset.y;
        offset.xy = offset.xy * 0.5 + 0.5;

        // Skip samples that project outside the screen
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0)
            continue;

        // Skip samples that land on a different object (silhouette rejection)
        uvec4 sampleIds = texture(idsTex, offset.xy);
        if (sampleIds.g != centerIds.g || sampleIds.b != centerIds.b)
            continue;

        // Skip samples that land on the background
        float sampledDepth = texture(depthTex, offset.xy).r;
        if (sampledDepth >= 1.0)
            continue;

        // Sample depth at projected position
        float sampleDepth = reconstructViewPos(offset.xy).z;

        // Range check: only occlude if within radius
        float rangeCheck = smoothstep(0.0, 1.0,
            u.radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + u.bias ? 1.0 : 0.0)
            * rangeCheck;
        validSamples++;
    }

    float ao = 1.0;
    if (validSamples > 0)
        ao = 1.0 - (occlusion / float(validSamples)) * u.intensity;
    ao = clamp(ao, 0.0, 1.0);

    FragAO = vec4(ao, ao, ao, 1.0);
}
