#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec2 fragUV;

// 向 4 个 G-Buffer 附件输出 (0 是 Swapchain，这里不用)
layout(location = 0) out vec4 outPosition; // 附件 1
layout(location = 1) out vec4 outNormal;   // 附件 2
layout(location = 2) out vec4 outAlbedo;   // 附件 3
layout(location = 3) out vec4 outPBR;      // 附件 4

// 只保留材质贴图
layout(set = 0, binding = 1) uniform sampler2D albedoMap;
layout(set = 0, binding = 2) uniform sampler2D normalMap;
layout(set = 0, binding = 3) uniform sampler2D metallicMap;
layout(set = 0, binding = 4) uniform sampler2D roughnessMap;

// 你的偏导数法线计算魔法保留在这里！
vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalMap, fragUV).xyz * 2.0 - 1.0;
    vec3 Q1  = dFdx(fragPosWorld);
    vec3 Q2  = dFdy(fragPosWorld);
    vec2 st1 = dFdx(fragUV);
    vec2 st2 = dFdy(fragUV);
    vec3 N   = normalize(fragNormalWorld);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

void main() {
    // 1. 位置 (世界坐标)
    outPosition = vec4(fragPosWorld, 1.0);

    // 2. 带有凹凸细节的世界法线
    outNormal = vec4(getNormalFromMap(), 1.0);

    // 3. 基础颜色
    vec4 albedoColor = texture(albedoMap, fragUV);
    outAlbedo = vec4(albedoColor.rgb * fragColor, albedoColor.a);

    // 4. PBR 参数打包 (R: 金属度, G: 粗糙度)
    float metallic = texture(metallicMap, fragUV).r;
    float roughness = texture(roughnessMap, fragUV).r;
    outPBR = vec4(metallic, roughness, 1.0, 1.0);
}