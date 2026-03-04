#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec2 outUV;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionView;
    vec4 ambientLightColor;
    vec3 lightDirection;
    vec4 lightColor;
    vec4 cameraPos;
} ubo;

void main() {
    outUV = uv;
    // 强行把球体绑定在摄像机中心
    vec3 worldPos = position + ubo.cameraPos.xyz;
    vec4 pos = ubo.projectionView * vec4(worldPos, 1.0);
    // 魔法：强制 z = w，让天空盒在透视除法后深度永远是 1.0 (最远)
    gl_Position = pos.xyww; 
}