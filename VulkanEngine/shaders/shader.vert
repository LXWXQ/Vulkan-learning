#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragPosWorld; // 传给片段着色器计算光照
layout(location = 2) out vec3 fragNormalWorld;
layout(location = 3) out vec2 fragUV;

// 【核心接入】：全局 UBO 数据
layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionView;
    vec4 ambientLightColor;
    vec3 lightDirection;
    vec4 lightColor;
} ubo;

// 极其轻量的 Push Constants，只保留物体自己的变换
layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    // 1. 先计算物体在世界中的绝对位置
    vec4 positionWorld = push.modelMatrix * vec4(inPosition, 1.0);
    
    // 2. 结合 UBO 里的摄像机矩阵得出最终屏幕坐标
    gl_Position = ubo.projectionView * positionWorld;

    // 3. 将法线转换到世界坐标系
    fragNormalWorld = normalize(mat3(push.normalMatrix) * inNormal);
    fragPosWorld = positionWorld.xyz;
    fragColor = inColor;
    fragUV = inUV;
}