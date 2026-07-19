#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// Per-triangle-mesh storage buffers (set=0 for vertex stage in SDL3_gpu SPIR-V)
layout(set = 0, binding = 0, std430) readonly buffer SrcVertexIdBuf { uint sourceVertexIds[]; };
layout(set = 0, binding = 1, std430) readonly buffer SrcPrimIndexBuf { uint sourcePrimitiveIds[]; };
layout(set = 0, binding = 2, std430) readonly buffer Attr0Buf { float attr0Data[]; };
layout(set = 0, binding = 3, std430) readonly buffer Attr1Buf { float attr1Data[]; };
layout(set = 0, binding = 4, std430) readonly buffer Attr2Buf { float attr2Data[]; };
layout(set = 0, binding = 5, std430) readonly buffer Attr3Buf { float attr3Data[]; };
layout(set = 0, binding = 6, std430) readonly buffer Attr4Buf { float attr4Data[]; };

// Vertex uniforms (set=1, binding=0 in SDL3_gpu SPIR-V convention)
layout(set = 1, binding = 0) uniform Transforms {
    mat4 MVP;   // proj * view * model
    mat4 MV;    // view * model
    mat4 M;     // model (world) transform
    uvec4 geomInfo;  // x=attrMask, y=hasSourcePrimitiveIds
    uvec4 attrInfo;  // x=attrKind (per-prim bitmask), y=packedComponents
} u;

layout(location = 0) out vec3 vNormalView;
layout(location = 1) out vec3 vPosView;
layout(location = 2) out vec4 vColor;
layout(location = 3) out vec4 vAttr0;
layout(location = 4) out vec4 vAttr1;
layout(location = 5) out vec4 vAttr2;
layout(location = 6) out vec4 vAttr3;
layout(location = 7) out vec3 vNormalWorld;
layout(location = 8) flat out uint vPrimitiveIndex;
layout(location = 9) flat out uint vPrimitiveId;

uint getComponentCount(uint slot) {
    return (u.attrInfo.y >> (slot * 4u)) & 0xFu;
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
    uint vid = gl_VertexIndex;
    uint triId = vid / 3u;
    uint sourcePrimIndex = (u.geomInfo.y != 0u) ? sourcePrimitiveIds[triId] : triId;
    uint posIdx = sourceVertexIds[vid];

    vec3 pos = inPosition;
    vec3 norm = inNormal;

    vec4 posView = u.MV * vec4(pos, 1.0);
    vPosView     = posView.xyz;
    vNormalView  = mat3(u.MV) * norm;
    vNormalWorld = mat3(u.M) * norm;

    uint attrMask = u.geomInfo.x;
    uint attrKind = u.attrInfo.x;

    vColor = vec4(0.0);
    vAttr0 = vec4(0.0);
    vAttr1 = vec4(0.0);
    vAttr2 = vec4(0.0);
    vAttr3 = vec4(0.0);

    if ((attrMask & 1u) != 0u) {
        uint ai = ((attrKind & 1u) != 0u) ? sourcePrimIndex : posIdx;
        uint nc = getComponentCount(0u);
        vColor = FETCH_ATTR(attr0Data, ai, nc);
    }
    if ((attrMask & 2u) != 0u) {
        uint ai = ((attrKind & 2u) != 0u) ? sourcePrimIndex : posIdx;
        uint nc = getComponentCount(1u);
        vAttr0 = FETCH_ATTR(attr1Data, ai, nc);
    }
    if ((attrMask & 4u) != 0u) {
        uint ai = ((attrKind & 4u) != 0u) ? sourcePrimIndex : posIdx;
        uint nc = getComponentCount(2u);
        vAttr1 = FETCH_ATTR(attr2Data, ai, nc);
    }
    if ((attrMask & 8u) != 0u) {
        uint ai = ((attrKind & 8u) != 0u) ? sourcePrimIndex : posIdx;
        uint nc = getComponentCount(3u);
        vAttr2 = FETCH_ATTR(attr3Data, ai, nc);
    }
    if ((attrMask & 16u) != 0u) {
        uint ai = ((attrKind & 16u) != 0u) ? sourcePrimIndex : posIdx;
        uint nc = getComponentCount(4u);
        vAttr3 = FETCH_ATTR(attr4Data, ai, nc);
    }

    vPrimitiveIndex = sourcePrimIndex;
    vPrimitiveId = sourcePrimIndex;
    gl_Position = u.MVP * vec4(pos, 1.0);
    gl_Position.y = -gl_Position.y;
}
