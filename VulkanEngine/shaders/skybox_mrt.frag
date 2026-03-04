#version 450
layout(location = 0) in vec2 inUV;

// 向 4 个 G-Buffer 附件同时输出
layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outPBR;

// 我们的环境贴图依然挂在全局户口本的 Binding 5 上
layout(set = 0, binding = 5) uniform sampler2D environmentMap;

void main() {
    outPosition = vec4(0.0);
    // 【核心信号】：法线输出全 0，告诉光照管线这是天空！
    outNormal = vec4(0.0, 0.0, 0.0, 1.0); 
    
    // 直接把原汁原味的环境光写进 Albedo 附件
    vec3 envColor = texture(environmentMap, inUV).rgb;
    outAlbedo = vec4(envColor, 1.0);
    
    outPBR = vec4(0.0);
}