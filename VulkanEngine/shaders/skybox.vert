#version 450

// 接收球体的顶点数据 (我们生成的 sphere.obj 包含了这些)
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec2 outUV;

// 0 号通道：必须和 PBR 的 UBO 结构完全一致，因为我们共用一个户口本
layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionView;
    vec4 ambientLightColor;
    vec3 lightDirection;
    vec4 lightColor;
    vec4 cameraPos; // 极度关键的摄像机坐标
} ubo;

void main() {
    outUV = uv; // 直接把生成的完美 UV 传给片段着色器
    
    // 【核心魔法：绝对跟随】
    // 将球体的中心，强行绑定在摄像机的坐标上！
    // 这样无论你怎么移动，你永远在球的绝对中心，永远走不到天空的尽头。
    vec3 worldPos = position + ubo.cameraPos.xyz;
    
    gl_Position = ubo.projectionView * vec4(worldPos, 1.0);
}