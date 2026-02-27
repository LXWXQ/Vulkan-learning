#version 450

layout(location = 0) in vec3 fragColor; // 从顶点着色器传过来的颜色
layout(location = 0) out vec4 outColor; // 输出到屏幕的最终颜色

void main() {
    outColor = vec4(fragColor, 1.0);
}