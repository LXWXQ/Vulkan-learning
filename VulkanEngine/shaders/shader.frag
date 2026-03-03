#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

// 0 号通道：全局数据
layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionView;
    vec4 ambientLightColor;
    vec3 lightDirection;
    vec4 lightColor;
    vec4 cameraPos; // 接收 C++ 传来的眼睛位置
} ubo;

// 1 到 4 号通道：PBR 四大核心材质
layout(binding = 1) uniform sampler2D albedoMap;    // 基础颜色
layout(binding = 2) uniform sampler2D normalMap;    // 凹凸细节
layout(binding = 3) uniform sampler2D metallicMap;  // 金属度
layout(binding = 4) uniform sampler2D roughnessMap; // 粗糙度

const float PI = 3.14159265359;

// ----------------------------------------------------------------------------
// 物理学黑科技：利用屏幕像素偏导数，直接从法线贴图计算出真实的 3D 凹凸
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

// ----------------------------------------------------------------------------
// PBR 核心公式 1：法线分布函数 (GGX) - 决定高光的扩散程度
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / max(denom, 0.0000001); // 防除零爆炸
}

// PBR 核心公式 2：几何遮蔽函数 (Schlick-GGX) - 模拟微小表面的自阴影
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// PBR 核心公式 3：菲涅尔方程 (Fresnel-Schlick) - 模拟边缘反光变强的物理现象
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ----------------------------------------------------------------------------
void main() {
    // 1. 抽取四大贴图的数据
    vec3 albedo     = texture(albedoMap, fragUV).rgb;
    float metallic  = texture(metallicMap, fragUV).r;
    float roughness = texture(roughnessMap, fragUV).r;
    
    // 2. 获取核心向量：法线(N)、观察方向(V)
    vec3 N = getNormalFromMap();
    vec3 V = normalize(ubo.cameraPos.xyz - fragPosWorld);

    // 3. 计算基础反射率 (F0)：非金属通常是 0.04，纯金属则直接等于自己的颜色
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    // 4. 光照计算 (由于只写了一个定向光，这里没写循环)
    vec3 L = normalize(-ubo.lightDirection); // 指向光源的向量
    vec3 H = normalize(V + L); // 半程向量
    vec3 radiance = ubo.lightColor.xyz * ubo.lightColor.w; // 光源辐射强度

    // --- Cook-Torrance BRDF 降临 ---
    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);       
    
    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator; // 最终的高光反射成分

    // 能量守恒定律：反射掉的光(kS) + 折射/漫反射的光(kD) = 1.0
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic; // 纯金属没有漫反射，光会全部穿透或反弹

    // 计算当前光线对当前像素的最终照度
    float NdotL = max(dot(N, L), 0.0);        
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // 5. 加入极度微弱的环境光，防止背光面死黑
    vec3 ambient = ubo.ambientLightColor.xyz * ubo.ambientLightColor.w * albedo * 0.1;

    // 6. 最终合成
    vec3 color = ambient + Lo;
    color = pow(color, vec3(1.0/2.2));
    outColor = vec4(color, 1.0);
}