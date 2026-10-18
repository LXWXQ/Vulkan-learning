#version 450

// 🌟 核心修复 1：恢复你原本的 3D 向量输入！(请确保你的 vert 着色器传过来的是 vec3 局部/世界坐标)
layout(location = 0) in vec3 inUVW;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outPBR;

layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionView;
    vec4 ambientLightColor;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 cameraPos;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D environmentMap;
const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {
    // 1. 利用你原本的逻辑，进行正确的 3D 向量球面采样！
    vec3 dir = normalize(inUVW);
    vec2 envUV = sampleSphericalMap(dir);
    vec3 envColor = texture(environmentMap, envUV).rgb;
    
    // 2. 直接透传线性 HDR 辐照度，不在这里做任何色调映射。
    //
    // D-2 修正：原来这里做了 ACES + gamma，理由是"天空盒会被 lighting.frag
    // 通过法线为 0 的标记原样透传为最终像素"。但 D-2 之后光照通道输出的是
    // 线性 HDR、由 PresentPass 统一做 tonemap，天空盒再提前 tonemap 一次
    // 就成了双重色调映射 —— 实测表现为天空被压暗发灰、边缘出现一圈假光晕
    // （因为被 clamp 到 1.0 的天空仍然超过 Bloom 阈值，整块天空都在起辉光）。
    //
    // 正确做法：天空盒和普通几何一样，只往 HDR 图里写线性值。
    // 为了让 envColor > 1 的高光能活到 Bloom，albedo 附件必须是浮点格式
    // （见 GBufferFormat::Albedo）—— 8bit UNORM 会在这里直接截断。
    
    // 3. 将处理好的终极颜色存入反照率通道
    outAlbedo = vec4(envColor, 1.0);
    
    // 4. 给 Lighting Pass 留下接头暗号：法线为 0
    outNormal = vec4(0.0, 0.0, 0.0, 1.0);
    
    // 5. 其余通道填 0 确保安全
    // 5. Store a far point along the sky direction so the motion-blur
    // reprojection pass gets a valid camera-relative world position.
    outPosition = vec4(ubo.cameraPos.xyz + normalize(inUVW) * 10000.0, 1.0);
    outPBR = vec4(0.0);
}
