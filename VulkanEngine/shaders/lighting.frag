#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor; 

struct PointLight 
{
    vec4 position;
    vec4 color;
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

layout(set = 0, binding = 1) uniform sampler2D environmentMap;

layout(set = 0, binding = 2) uniform sampler2D inPosition;
layout(set = 0, binding = 3) uniform sampler2D inNormal;
layout(set = 0, binding = 4) uniform sampler2D inAlbedo;
layout(set = 0, binding = 5) uniform sampler2D inPbr;
layout(set = 0, binding = 6) uniform sampler2D inSSAO;

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

vec3 CalcDirectionalLight(vec3 N, vec3 V, vec3 albedo, float roughness, float metallic, vec3 F0) {
    vec3 L = normalize(-ubo.lightDirection.xyz);
    vec3 radiance = ubo.lightColor.xyz * ubo.lightColor.w;
    return CalcLightBRDF(L, V, N, albedo, roughness, metallic, F0, radiance);
}

vec3 CalcPointLight(PointLight light, vec3 N, vec3 fragPos, vec3 V, vec3 albedo, float roughness, float metallic, vec3 F0) {
    vec3 lightPos = light.position.xyz;
    float lightRadius = 30.0;
    vec3 lightColor = light.color.xyz;
    float lightIntensity = light.color.w;
    vec3 L = lightPos - fragPos;
    float distance = length(L);
    if (distance >= lightRadius) return vec3(0.0);
    L = normalize(L);
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
    vec3 fragPosWorld = texture(inPosition, inUV).xyz;
    vec3 N            = texture(inNormal, inUV).xyz;
    vec3 albedo       = texture(inAlbedo, inUV).rgb;
    vec4 pbr          = texture(inPbr, inUV); 
    float ssao        = texture(inSSAO, inUV).r;
    float metallic  = pbr.r;
    float roughness = pbr.g;
    roughness = max(roughness, 0.04);
    
    if (length(N) < 0.1) {
        outColor = vec4(albedo, 1.0);
        return;
    }

    vec3 V = normalize(ubo.cameraPos.xyz - fragPosWorld);
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);
    Lo += CalcDirectionalLight(N, V, albedo, roughness, metallic, F0);

    for (int i = 0; i < ubo.numLights; i++)
        Lo += CalcPointLight(ubo.pointLights[i], N, fragPosWorld, V, albedo, roughness, metallic, F0);

    vec3 F_ambient = fresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kD_ambient = vec3(1.0) - F_ambient;
    kD_ambient *= 1.0 - metallic;

    vec3 R = reflect(-V, N);
    vec3 envDiffuse = texture(environmentMap, SampleSphericalMap(N)).rgb;
    vec3 envSpecular = texture(environmentMap, SampleSphericalMap(R)).rgb;
    envSpecular *= (1.0 - roughness); 
    
    vec3 ambient = (kD_ambient * envDiffuse * albedo + envSpecular * F_ambient) * 0.5;

    vec3 color = ambient + Lo;
    color = ACESFilm(color);
    color = pow(color, vec3(1.0/2.2));
    color *= ssao;
    outColor = vec4(color, 1.0);
}
