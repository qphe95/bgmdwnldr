#version 450

layout(binding = 0) uniform sampler2D fontTex;

layout(location = 0) in vec2 outUv;
layout(location = 0) out vec4 outColor;

void main() {
    float a = texture(fontTex, outUv).r;
    vec3 neon = vec3(0.10, 0.95, 1.00);
    float scan = 0.85 + 0.15 * step(0.5, fract(gl_FragCoord.y / 2.0));
    outColor = vec4(neon * scan, a);
}
