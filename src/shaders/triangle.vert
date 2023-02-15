#version 460

layout(set = 0, binding = 0) uniform ModelViewProjectionObject {
    mat4 model;
    mat4 view;
    mat4 projection;
} mvproj;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = mvproj.projection * mvproj.view * mvproj.model * vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
}