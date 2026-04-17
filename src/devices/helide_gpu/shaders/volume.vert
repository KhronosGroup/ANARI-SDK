#version 450

layout(location = 0) in vec3 aPos; // unit cube vertex in [0,1]^3

layout(set = 1, binding = 0) uniform Transforms
{
  mat4 MVP;
  mat4 M; // fieldToWorld * instanceXfm
} u;

layout(location = 0) out vec3 vLocalPos; // position in [0,1]^3 (= texture coord)

void main()
{
  vLocalPos = aPos;
  gl_Position = u.MVP * vec4(aPos, 1.0);
  gl_Position.y = -gl_Position.y; // Vulkan Y-flip
}
