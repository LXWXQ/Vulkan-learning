#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push {
    mat2 transform;
    vec2 offset;
    vec3 color;
} push;

void main() {
    // 将顶点本身的颜色与我们推送的颜色进行混合，展现出动态效果
    outColor = vec4(fragColor * push.color, 1.0);
}