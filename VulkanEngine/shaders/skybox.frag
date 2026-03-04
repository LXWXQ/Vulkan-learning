#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// 只需要接入 5 号通道的 32MB 浮点核弹！
layout(set = 0, binding = 5) uniform sampler2D environmentMap;

void main() {
    // 1. 极其暴力的纯粹抓取
    vec3 envColor = texture(environmentMap, inUV).rgb;
    
    // 2. 苍穹同样需要佩戴“光学矫正镜片”，否则会亮瞎或者死黑！
    // Tone Mapping (HDR 映射到 LDR)
    envColor = envColor / (envColor + vec3(1.0));
    // Gamma 矫正 (线性空间变 sRGB 空间)
    envColor = pow(envColor, vec3(1.0/2.2));
    
    outColor = vec4(envColor, 1.0);
}