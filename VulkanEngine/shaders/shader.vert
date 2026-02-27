#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

// 接收 Push Constants
layout(push_constant) uniform Push {
    mat2 transform;
    vec2 offset;
    vec3 color;
} push;

void main() {
    // 矩阵乘法：先旋转，再平移
    gl_Position = vec4(push.transform * inPosition + push.offset, 0.0, 1.0);
    fragColor = inColor;
}