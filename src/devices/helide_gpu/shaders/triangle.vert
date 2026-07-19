#version 450

// Dummy per-vertex input. The triangle pipeline fetches all real vertex data
// from SSBOs via gl_VertexIndex, but declaring a vertex input (and binding a
// dummy VBO at draw time) forces spirv-cross to emit MSL with [[stage_in]],
// matching the shape the fix_msl_buffers.py reorderer expects on macOS/Metal.
layout(location = 0) in vec3 _dummyPos;

// Per-array storage buffers (set=0 for vertex stage in SDL3_gpu SPIR-V)
layout(set = 0, binding = 0, std430) readonly buffer PosBuf   { float positions[]; };
layout(set = 0, binding = 1, std430) readonly buffer NormBuf  { float normals[];   };
layout(set = 0, binding = 2, std430) readonly buffer IdxBuf   { uint  indices[];   };
layout(set = 0, binding = 3, std430) readonly buffer Attr0Buf { float attr0Data[]; };
layout(set = 0, binding = 4, std430) readonly buffer Attr1Buf { float attr1Data[]; };
layout(set = 0, binding = 5, std430) readonly buffer Attr2Buf { float attr2Data[]; };
layout(set = 0, binding = 6, std430) readonly buffer Attr3Buf { float attr3Data[]; };
layout(set = 0, binding = 7, std430) readonly buffer Attr4Buf { float attr4Data[]; };
layout(set = 0, binding = 8, std430) readonly buffer SrcPrimBuf { uint sourcePrimitiveIds[]; };

// Vertex uniforms (set=1, binding=0 in SDL3_gpu SPIR-V convention)
layout(set = 1, binding = 0) uniform Transforms {
    mat4 MVP;   // proj * view * model
    mat4 MV;    // view * model
    mat4 M;     // model (world) transform
    uvec4 geomInfo;  // x=posCount, y=hasIndices, z=hasNormals, w=attrMask
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

vec3 fetchPos(uint vertIdx) {
    uint b = vertIdx * 3u;
    return vec3(positions[b], positions[b + 1u], positions[b + 2u]);
}

vec3 fetchNorm(uint vertIdx) {
    uint b = vertIdx * 3u;
    return vec3(normals[b], normals[b + 1u], normals[b + 2u]);
}

uint getComponentCount(uint slot) {
    return (u.attrInfo.y >> (slot * 4u)) & 0xFu;
}

// Read nc floats from a buffer starting at base, pad to vec4 per ANARI convention:
//   1 -> (x, 0, 0, 1),  2 -> (x, y, 0, 1),
//   3 -> (x, y, z, 1),  4 -> (x, y, z, w)
#define FETCH_ATTR(buf, idx, nc) \
    _fetchAttr(buf[(idx)*(nc)], \
               ((nc) >= 2u) ? buf[(idx)*(nc)+1u] : 0.0, \
               ((nc) >= 3u) ? buf[(idx)*(nc)+2u] : 0.0, \
               ((nc) >= 4u) ? buf[(idx)*(nc)+3u] : 1.0, nc)

vec4 _fetchAttr(float x, float y, float z, float w, uint nc) {
    // Fix w: if nc < 4, w was set to 1.0 by the macro already;
    // if nc == 1, zero out y,z (macro passed 0.0 for them).
    return vec4(x, y, z, (nc < 4u) ? 1.0 : w);
}

void main()
{
    uint vid = gl_VertexIndex;
    uint triId = vid / 3u;
    bool hasSourcePrimIds = (u.attrInfo.z != 0u);
    uint sourcePrimId = hasSourcePrimIds ? sourcePrimitiveIds[triId] : triId;

    // Resolve vertex index (apply index buffer indirection if present)
    bool hasIdx = (u.geomInfo.y != 0u);
    uint posIdx = hasIdx ? indices[vid] : vid;

    // Fetch position
    vec3 pos = fetchPos(posIdx);

    // Fetch or compute normal
    vec3 norm;
    if (u.geomInfo.z != 0u) {
        // Per-vertex normals from normal SSBO
        norm = fetchNorm(posIdx);
    } else {
        // Compute flat face normal from triangle positions
        uint base = triId * 3u;
        uint i0 = hasIdx ? indices[base]        : base;
        uint i1 = hasIdx ? indices[base + 1u]   : base + 1u;
        uint i2 = hasIdx ? indices[base + 2u]   : base + 2u;
        vec3 v0 = fetchPos(i0);
        vec3 v1 = fetchPos(i1);
        vec3 v2 = fetchPos(i2);
        norm = normalize(cross(v1 - v0, v2 - v0));
    }

    vec4 posView = u.MV * vec4(pos, 1.0);
    vPosView     = posView.xyz;
    vNormalView  = mat3(u.MV) * norm;
    vNormalWorld = mat3(u.M) * norm;

    // Fetch attributes from per-array SSBOs
    uint attrMask = u.geomInfo.w;
    uint attrKind = u.attrInfo.x;

    vColor = vec4(0.0);
    vAttr0 = vec4(0.0);
    vAttr1 = vec4(0.0);
    vAttr2 = vec4(0.0);
    vAttr3 = vec4(0.0);

    // Each attribute: choose vertex (posIdx) or primitive (triId) indexing
    if ((attrMask & 1u) != 0u) {
        uint ai = ((attrKind & 1u) != 0u) ? sourcePrimId : posIdx;
        uint nc = getComponentCount(0u);
        vColor = FETCH_ATTR(attr0Data, ai, nc);
    }
    if ((attrMask & 2u) != 0u) {
        uint ai = ((attrKind & 2u) != 0u) ? sourcePrimId : posIdx;
        uint nc = getComponentCount(1u);
        vAttr0 = FETCH_ATTR(attr1Data, ai, nc);
    }
    if ((attrMask & 4u) != 0u) {
        uint ai = ((attrKind & 4u) != 0u) ? sourcePrimId : posIdx;
        uint nc = getComponentCount(2u);
        vAttr1 = FETCH_ATTR(attr2Data, ai, nc);
    }
    if ((attrMask & 8u) != 0u) {
        uint ai = ((attrKind & 8u) != 0u) ? sourcePrimId : posIdx;
        uint nc = getComponentCount(3u);
        vAttr2 = FETCH_ATTR(attr3Data, ai, nc);
    }
    if ((attrMask & 16u) != 0u) {
        uint ai = ((attrKind & 16u) != 0u) ? sourcePrimId : posIdx;
        uint nc = getComponentCount(4u);
        vAttr3 = FETCH_ATTR(attr4Data, ai, nc);
    }

    vPrimitiveIndex = sourcePrimId;
    vPrimitiveId = sourcePrimId;

    gl_Position   = u.MVP * vec4(pos, 1.0);
    gl_Position.y = -gl_Position.y;

    // Keep _dummyPos live so glslang/spirv-cross don't strip the vertex input.
    gl_Position.xyz += _dummyPos * 0.0;
}
