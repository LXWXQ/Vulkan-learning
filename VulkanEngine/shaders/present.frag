#version 450

// D-2 的收尾 Pass：把线性 HDR 场景 + Bloom 叠加后，做曝光 -> ACES -> gamma，
// 输出到交换链。ImGui 的 UI 也画在这个 Pass 里（叠加在最终图像之上）。
//
// 为什么 tonemap/gamma 必须独立成一个 Pass 而不是塞在 lighting.frag 里：
//   Bloom 需要在「HDR、未 clamp」的数据上工作（见 lighting.frag 结尾的说明），
//   所以光照通道的输出不能再是最终显示值。把 tonemap 抽到这里之后，
//   HDR 场景图成了一个可复用的中间产物 —— 以后加 SSR / 自动曝光 / 颜色分级
//   都是往 present 之前的链路上插，而不是回头改光照着色器。

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;

layout(push_constant) uniform Push {
    float exposure;
    float bloomIntensity;
    float bloomEnabled;   // 0 / 1，避免为了关 Bloom 再建一条管线
    float pad;
} pc;

// ACES Filmic 近似（Narkowicz 2015）。
// 注意它最后会 clamp 到 [0,1] —— 这一步之后高光信息就没了，
// 所以它必须是整条链路的最后一步，任何后效都得排在它前面。
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

    if (pc.bloomEnabled > 0.5)
        hdr += texture(bloomTex, inUV).rgb * pc.bloomIntensity;

    vec3 color = ACESFilm(hdr * pc.exposure);
    // 交换链是 UNORM 格式（硬件不做 sRGB 编码），gamma 必须手动做
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
