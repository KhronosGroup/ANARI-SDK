#version 450

// A visible quad light (KHR_LIGHT_PRIMARY_VISIBILITY): its two triangles,
// built from the uniforms with no vertex buffer

// Vertex uniforms (set=1, binding=0 in SDL3_gpu SPIR-V convention)
layout(set = 1, binding = 0) uniform LightQuadTransforms {
    mat4 P;
    // view-space corner (xyz) and the edges from it (xyz)
    vec4 origin;
    vec4 edge1;
    vec4 edge2;
} u;

void main()
{
    const vec2 corners[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0),
        vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    vec2 t = corners[gl_VertexIndex];
    vec3 p = u.origin.xyz + t.x * u.edge1.xyz + t.y * u.edge2.xyz;
    gl_Position = u.P * vec4(p, 1.0);
    gl_Position.y = -gl_Position.y;
}
