#version 450

// TD-06: Bloom disabled present path. This shader deliberately has only the
// HDR binding, so PresentPass never binds bloomResult and BloomPass can skip
// every Bloom pass when the feature is off.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

layout(push_constant) uniform Push {
    float exposure;
    float bloomIntensity;   // kept for layout parity; unused here
    float bloomEnabled;     // kept for layout parity; always 0
    float pad;
} pc;

vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(hdrScene, inUV).rgb;
    vec3 color = ACESFilm(hdr * pc.exposure);
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}