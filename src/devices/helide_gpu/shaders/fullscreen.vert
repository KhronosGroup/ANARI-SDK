#version 450

layout(location = 0) out vec2 vUV;

void main()
{
    // Generate fullscreen triangle from gl_VertexIndex (0, 1, 2)
    // Produces vertices at (-1,-1), (3,-1), (-1,3) covering the entire screen
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUV = vec2(pos.x, 1.0 - pos.y);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
