#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor; // 最终输出到 Swapchain (附件 0)

// 全局光照和摄像机参数
layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionView;
    vec4 ambientLightColor;
    vec3 lightDirection;
    vec4 lightColor;
    vec4 cameraPos; 
} ubo;

// 环境光贴图依然需要
layout(set = 0, binding = 5) uniform sampler2D environmentMap;

// 【核心】：声明 Input Attachments (读取 Subpass 0 的输出)
// 注意：binding 号码我们在下一步创建 DescriptorSetLayout 时会分配为 6, 7, 8, 9
layout(input_attachment_index = 0, set = 0, binding = 6) uniform subpassInput inPosition;
layout(input_attachment_index = 1, set = 0, binding = 7) uniform subpassInput inNormal;
layout(input_attachment_index = 2, set = 0, binding = 8) uniform subpassInput inAlbedo;
layout(input_attachment_index = 3, set = 0, binding = 9) uniform subpassInput inPBR;

// ============================================================================
// 你的 PBR 核心公式 (原封不动移植)
const float PI = 3.14159265359;
const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
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

void main() {
    // 1. 【核心变更】：从 G-Buffer 读取数据！
    // subpassLoad() 是 Vulkan 特有的极速指令，直接从当前像素所在的显存块读取
    vec3 fragPosWorld = subpassLoad(inPosition).xyz;
    vec3 N            = subpassLoad(inNormal).xyz;
    vec3 albedo       = subpassLoad(inAlbedo).rgb;
    vec4 pbr          = subpassLoad(inPBR);
    
    float metallic  = pbr.r;
    float roughness = pbr.g;

    // 如果该像素没有写入数据（比如背景区域法线长度为 0），则直接输出背景色
    if (length(N) < 0.1) {
        // G-Buffer 的 Albedo 里已经存好了我们在 skybox_mrt.frag 中采样的环境图
        vec3 skyColor = albedo;
        
        // HDR 色调映射 (Tone Mapping)
        skyColor = skyColor / (skyColor + vec3(1.0));
        // Gamma 矫正
        skyColor = pow(skyColor, vec3(1.0/2.2));
        
        outColor = vec4(skyColor, 1.0);
        return;
    }

    // 2. 剩下的就是你完美的 PBR 光照流程了
    vec3 V = normalize(ubo.cameraPos.xyz - fragPosWorld);
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    vec3 L = normalize(-ubo.lightDirection);
    vec3 H = normalize(V + L);
    vec3 radiance = ubo.lightColor.xyz * ubo.lightColor.w;

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
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // IBL
    vec3 R = reflect(-V, N);
    vec3 envDiffuse = texture(environmentMap, SampleSphericalMap(N)).rgb;
    vec3 envSpecular = texture(environmentMap, SampleSphericalMap(R)).rgb;
    envSpecular *= (1.0 - roughness); 
    
    vec3 ambient = (kD * envDiffuse * albedo + envSpecular * F) * 0.5;

    // 合成与 Gamma 矫正
    vec3 color = ambient + Lo;
    color = pow(color, vec3(1.0/2.2));
    outColor = vec4(color, 1.0);
}