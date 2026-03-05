#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec2 uv;
struct PointLight {
    vec4 position; // xyz: position, w: radius
    vec4 color;    // xyz: color, w: intensity
};

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionView;
    vec4 ambientLightColor;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 cameraPos;
    int numLights;
    PointLight pointLights[100];
} ubo;

layout(location = 0) out vec3 fragColor;

// light_cube.vert
void main() {
    PointLight light = ubo.pointLights[gl_InstanceIndex];
    
    float debugScale = 0.1; 
    vec3 worldPos = (position * debugScale) + light.position.xyz;
    gl_Position = ubo.projectionView * vec4(worldPos, 1.0);
    
    // ✨ 核心修复：不要直接乘以 50.0 的 intensity
    // 我们只给它一个微弱的增强（比如 2.0），让它看起来亮，但保留色彩
    fragColor = light.color.xyz; 
}