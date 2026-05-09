#version 450

// 🌟 核心修复 1：恢复你原本的 3D 向量输入！(请确保你的 vert 着色器传过来的是 vec3 局部/世界坐标)
layout(location = 0) in vec3 inUVW;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outPBR;
layout(set = 0, binding = 1) uniform sampler2D environmentMap;
const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

// 保持和场景相同的 ACES 电影级色调映射
vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    // 1. 利用你原本的逻辑，进行正确的 3D 向量球面采样！
    vec3 dir = normalize(inUVW);
    vec2 envUV = sampleSphericalMap(dir);
    vec3 envColor = texture(environmentMap, envUV).rgb;
    
    // 2. 提前映射：压回 0.0~1.0 范围，防止 G-Buffer 的 8-bit Albedo 截断高光
    envColor = ACESFilm(envColor);
    envColor = pow(envColor, vec3(1.0/2.2));
    
    // 3. 将处理好的终极颜色存入反照率通道
    outAlbedo = vec4(envColor, 1.0);
    
    // 4. 给 Lighting Pass 留下接头暗号：法线为 0
    outNormal = vec4(0.0, 0.0, 0.0, 1.0);
    
    // 5. 其余通道填 0 确保安全
    outPosition = vec4(0.0);
    outPBR = vec4(0.0);
}