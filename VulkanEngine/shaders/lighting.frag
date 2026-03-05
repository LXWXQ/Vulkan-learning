#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor; 

struct PointLight 
{
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

layout(set = 0, binding = 5) uniform sampler2D environmentMap;

layout(input_attachment_index = 0, set = 0, binding = 6) uniform subpassInput inPosition;
layout(input_attachment_index = 1, set = 0, binding = 7) uniform subpassInput inNormal;
layout(input_attachment_index = 2, set = 0, binding = 8) uniform subpassInput inAlbedo;
layout(input_attachment_index = 3, set = 0, binding = 9) uniform subpassInput inPBR;

// ============================================================================
// PBR 基础数学公式
// ============================================================================
const float PI = 3.14159265359;
const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(clamp(v.y, -1.0, 1.0)));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / max(denom, 0.0000001);
}

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

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// 核心模块化封装：计算单盏灯光的 BRDF 辐射度
// ============================================================================
vec3 CalcLightBRDF(vec3 L, vec3 V, vec3 N, vec3 albedo, float roughness, float metallic, vec3 F0, vec3 radiance) {
    vec3 H = normalize(V + L);
            
    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);        
            
    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);        
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// ============================================================================
// 封装：计算太阳光 (定向光)
// ============================================================================
vec3 CalcDirectionalLight(vec3 N, vec3 V, vec3 albedo, float roughness, float metallic, vec3 F0) {
    vec3 L = normalize(-ubo.lightDirection.xyz);
    vec3 radiance = ubo.lightColor.xyz * ubo.lightColor.w;
    return CalcLightBRDF(L, V, N, albedo, roughness, metallic, F0, radiance);
}

// ============================================================================
// 封装：计算衰减的点光源
// ============================================================================
vec3 CalcPointLight(PointLight light, vec3 N, vec3 fragPos, vec3 V, vec3 albedo, float roughness, float metallic, vec3 F0) {
    vec3 lightPos = light.position.xyz;
    float lightRadius = 30.0;
    vec3 lightColor = light.color.xyz;
    float lightIntensity = light.color.w;

    vec3 L = lightPos - fragPos;
    float distance = length(L);
    
    // 如果像素超出了光照半径，直接跳过昂贵的物理计算，极致优化性能
    if (distance >= lightRadius) {
        return vec3(0.0);
    }

    L = normalize(L);
    
    // 距离平方反比定律 + 边缘平滑衰减
    float distSquared = distance * distance;
    float radiusSquared = lightRadius * lightRadius;
    float attenuation = pow(clamp(1.0 - pow(distSquared / radiusSquared, 2.0), 0.0, 1.0), 2.0);
    attenuation /= (distSquared + 1.0); 

    vec3 radiance = lightColor * lightIntensity * attenuation;
    return CalcLightBRDF(L, V, N, albedo, roughness, metallic, F0, radiance);
}

vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    // 1. 读取 G-Buffer
    vec3 fragPosWorld = subpassLoad(inPosition).xyz;
    vec3 N            = subpassLoad(inNormal).xyz;
    vec3 albedo       = subpassLoad(inAlbedo).rgb;
    vec4 pbr          = subpassLoad(inPBR);
    
    float metallic  = pbr.r;
    float roughness = pbr.g;
    roughness = max(roughness, 0.04);
    
    // 天空盒直接放行
    if (length(N) < 0.1) {
        vec3 skyColor = albedo;
        skyColor = skyColor / (skyColor + vec3(1.0));
        skyColor = pow(skyColor, vec3(1.0/2.2));
        outColor = vec4(skyColor, 1.0);
        return;
    }

    // 2. 准备摄像机视角参数
    vec3 V = normalize(ubo.cameraPos.xyz - fragPosWorld);
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    // 总辐射度累加器
    vec3 Lo = vec3(0.0);

    // 3. 累加定向光
    Lo += CalcDirectionalLight(N, V, albedo, roughness, metallic, F0);

    // 4. 累加所有动态点光源
    for (int i = 0; i < ubo.numLights; i++) 
    {
        Lo += CalcPointLight(ubo.pointLights[i], N, fragPosWorld, V, albedo, roughness, metallic, F0);
    }

    // 5. 环境光 IBL (基于视角的独立菲涅尔计算)
    vec3 F_ambient = fresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kD_ambient = vec3(1.0) - F_ambient;
    kD_ambient *= 1.0 - metallic;

    vec3 R = reflect(-V, N);
    vec3 envDiffuse = texture(environmentMap, SampleSphericalMap(N)).rgb;
    vec3 envSpecular = texture(environmentMap, SampleSphericalMap(R)).rgb;
    envSpecular *= (1.0 - roughness); 
    
    vec3 ambient = (kD_ambient * envDiffuse * albedo + envSpecular * F_ambient) * 0.5;

    // 6. 最终合成与色调映射
    vec3 color = ambient + Lo;
    color = ACESFilm(color);
    color = pow(color, vec3(1.0/2.2));
    outColor = vec4(color, 1.0);
}