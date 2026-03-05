#version 450

layout(location = 0) in vec3 inUVW;

// 输出到 MRT (和你的 Geometry Pass 保持一致)
layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outPBR;

// 绑定环境光 HDR 贴图 (在你的描述符集中，它是 binding = 5)
layout(set = 0, binding = 5) uniform sampler2D environmentMap;

// 经纬度映射常数
const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {
    // 归一化采样向量
    vec3 dir = normalize(inUVW);
    
    // 3D 向量转 2D UV
    vec2 envUV = sampleSphericalMap(dir);
    
    // 采样 HDR (因为 HDR 可能是线性空间，直接输出即可)
    vec3 envColor = texture(environmentMap, envUV).rgb;
    
    outAlbedo = vec4(envColor, 1.0);
    
    // 天空盒不参与光照计算，其余 G-Buffer 填默认值
    outPosition = vec4(0.0);
    outNormal = vec4(0.0);
    outPBR = vec4(0.0);
}