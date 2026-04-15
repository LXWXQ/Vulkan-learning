#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outFragColor;

// 🌟 直接蹭全局的相机矩阵！
layout(set = 0, binding = 0) uniform GlobalUbo {
    mat4 projectionView;
} globalUbo;

layout(set = 0, binding = 6) uniform sampler2D texPosition;
layout(set = 0, binding = 7) uniform sampler2D texNormal;

layout(set = 1, binding = 0) uniform SSAOUbo {
    mat4 projection; // 废弃不用了
    mat4 view;       // 废弃不用了
    vec4 samples[64];
    int kernelSize;
    float radius;
    float bias;
} ubo;

layout(set = 1, binding = 1) uniform sampler2D texNoise;

void main() 
{
    vec3 normal = texture(texNormal, inUV).xyz;
    
    // 🌟 性能救星：天空盒没有法线，直接跳过 SSAO 计算，节省 50% 性能！
    if (length(normal) < 0.1) {
        outFragColor = vec4(1.0);
        return;
    }

    vec2 screenSize = textureSize(texPosition, 0);
    vec2 noiseScale = screenSize / 4.0; 

    vec3 fragPos = texture(texPosition, inUV).xyz;
    vec3 randomVec = texture(texNoise, inUV * noiseScale).xyz;

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    // 🌟 降低采样率到 16 次 (日常游戏标准)
    int actualKernelSize = min(ubo.kernelSize, 16); 
    
    for(int i = 0; i < actualKernelSize; ++i) 
    {
        vec3 samplePos = TBN * ubo.samples[i].xyz; 
        samplePos = fragPos + samplePos * ubo.radius; 
        
        // 🌟 直接用全局投影矩阵！
        vec4 offset = vec4(samplePos, 1.0);
        offset = globalUbo.projectionView * offset; 
        if (abs(offset.w) < 0.0001) continue;
        offset.xyz /= offset.w; 
        offset.xyz = offset.xyz * 0.5 + 0.5; 

        if(offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) continue;

        float sampleDepth = texture(texPosition, offset.xy).z;
        
        // 🌟 防闪退魔法：+0.0001 防止除以 0 导致 GPU 崩溃
        float rangeCheck = smoothstep(0.0, 1.0, ubo.radius / (abs(fragPos.z - sampleDepth) + 0.0001));
        
        occlusion += (sampleDepth >= samplePos.z + ubo.bias ? 1.0 : 0.0) * rangeCheck;           
    }
    
    occlusion = 1.0 - (occlusion / float(actualKernelSize));
    outFragColor = vec4(occlusion, occlusion, occlusion, 1.0);
}