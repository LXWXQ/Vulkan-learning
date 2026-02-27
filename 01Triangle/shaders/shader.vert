#version 450

// 硬编码三角形的三个顶点位置
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),  // 顶部
    vec2(0.5, 0.5),   // 右下
    vec2(-0.5, 0.5)   // 左下
);

// 硬编码三个顶点的颜色
vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0), // 红
    vec3(0.0, 1.0, 0.0), // 绿
    vec3(0.0, 0.0, 1.0)  // 蓝
);

layout(location = 0) out vec3 fragColor;

void main() {
    // gl_VertexIndex 是内置变量，表示当前处理的是第几个点
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}