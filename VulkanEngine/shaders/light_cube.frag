#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

// ✨ 引入 ACES 曲线，把 50.0 的强度压回到 1.0 以内
vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// light_cube.frag
void main() {
    // 使用 ACES 压制高光
    //vec3 color = ACESFilm(fragColor);
    outColor = vec4(fragColor, 1.0);
}