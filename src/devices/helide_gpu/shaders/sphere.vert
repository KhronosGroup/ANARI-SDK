#version 450

// Icosphere mesh vertex (still via vertex buffer for instanced draw)
layout(location = 0) in vec3 icoPos;

// Per-sphere storage buffers (set=0 for vertex stage in SDL3_gpu SPIR-V)
layout(set = 0, binding = 0, std430) readonly buffer PosBuf   { float positions[];  };
layout(set = 0, binding = 1, std430) readonly buffer RadBuf   { float radii[];      };
layout(set = 0, binding = 2, std430) readonly buffer Attr0Buf { float attr0Data[];  };
layout(set = 0, binding = 3, std430) readonly buffer Attr1Buf { float attr1Data[];  };
layout(set = 0, binding = 4, std430) readonly buffer Attr2Buf { float attr2Data[];  };
layout(set = 0, binding = 5, std430) readonly buffer Attr3Buf { float attr3Data[];  };
layout(set = 0, binding = 6, std430) readonly buffer Attr4Buf { float attr4Data[];  };

// Vertex uniforms: set=1, binding=0
layout(set = 1, binding = 0) uniform Transforms {
    mat4 MVP;   // proj * view * model
    mat4 MV;    // view * model
    mat4 M;     // model (world) transform
    mat4 P;     // projection matrix (for gl_FragDepth reconstruction)
    uvec4 sphereInfo;  // x=hasRadii, y=attrMask, z=attrKind, w=packedComponents
    vec4  defaults;    // x=uniformRadius
} u;

layout(location = 0) flat out vec3 vCenterView;
layout(location = 1) flat out float vRadius;
layout(location = 2) out vec3 vPosView;
layout(location = 3) flat out vec4 vColor;
layout(location = 4) flat out vec4 vAttr0;
layout(location = 5) flat out vec4 vAttr1;
layout(location = 6) flat out vec4 vAttr2;
layout(location = 7) flat out vec4 vAttr3;
layout(location = 8) flat out vec3 vCenterWorld;
layout(location = 9) flat out mat4 vProj;
layout(location = 13) flat out int vInstanceIndex;

uint getComponentCount(uint slot) {
    return (u.sphereInfo.w >> (slot * 4u)) & 0xFu;
}

#define FETCH_ATTR(buf, idx, nc) \
    _fetchAttr(buf[(idx)*(nc)], \
               ((nc) >= 2u) ? buf[(idx)*(nc)+1u] : 0.0, \
               ((nc) >= 3u) ? buf[(idx)*(nc)+2u] : 0.0, \
               ((nc) >= 4u) ? buf[(idx)*(nc)+3u] : 1.0, nc)

vec4 _fetchAttr(float x, float y, float z, float w, uint nc) {
    return vec4(x, y, z, (nc < 4u) ? 1.0 : w);
}

void main()
{
    uint sid = gl_InstanceIndex;

    // Fetch per-sphere center from positions SSBO
    uint pb = sid * 3u;
    vec3 center = vec3(positions[pb], positions[pb + 1u], positions[pb + 2u]);

    // Fetch radius from radii SSBO or use uniform fallback
    float r = (u.sphereInfo.x != 0u) ? radii[sid] : u.defaults.x;

    // Scale icosphere vertex to enclose the sphere (1.26 = circumradius factor)
    vec3 worldPos = center + icoPos * r * 1.26;

    vec4 posView = u.MV * vec4(worldPos, 1.0);
    vPosView = posView.xyz;

    vCenterView = (u.MV * vec4(center, 1.0)).xyz;
    vCenterWorld = (u.M * vec4(center, 1.0)).xyz;
    vRadius = r;

    // Fetch attributes from per-array SSBOs
    uint attrMask = u.sphereInfo.y;

    vColor = vec4(0.0);
    vAttr0 = vec4(0.0);
    vAttr1 = vec4(0.0);
    vAttr2 = vec4(0.0);
    vAttr3 = vec4(0.0);

    // For spheres, per-vertex and per-primitive both index by sphere ID
    if ((attrMask & 1u) != 0u) {
        uint nc = getComponentCount(0u);
        vColor = FETCH_ATTR(attr0Data, sid, nc);
    }
    if ((attrMask & 2u) != 0u) {
        uint nc = getComponentCount(1u);
        vAttr0 = FETCH_ATTR(attr1Data, sid, nc);
    }
    if ((attrMask & 4u) != 0u) {
        uint nc = getComponentCount(2u);
        vAttr1 = FETCH_ATTR(attr2Data, sid, nc);
    }
    if ((attrMask & 8u) != 0u) {
        uint nc = getComponentCount(3u);
        vAttr2 = FETCH_ATTR(attr3Data, sid, nc);
    }
    if ((attrMask & 16u) != 0u) {
        uint nc = getComponentCount(4u);
        vAttr3 = FETCH_ATTR(attr4Data, sid, nc);
    }

    vProj = u.P;
    vInstanceIndex = int(sid);

    gl_Position = u.MVP * vec4(worldPos, 1.0);
    gl_Position.y = -gl_Position.y;  // flip Y: Vulkan NDC is Y-down, ANARI is Y-up
}
