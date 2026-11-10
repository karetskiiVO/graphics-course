#version 450
#extension GL_GOOGLE_include_directive : require

#include "shadertoy.glsl"

float Hash2(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float Noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = Hash2(i + vec2(0.0, 0.0));
    float b = Hash2(i + vec2(1.0, 0.0));
    float c = Hash2(i + vec2(0.0, 1.0));
    float d = Hash2(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float FBM2(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * Noise2(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    float ang   = uv.x * 6.2831853;
    float stave = smoothstep(0.0, 0.15, abs(sin(ang * 5.0)));

    float grain = FBM2(vec2(uv.x * 10.0, uv.y * 3.0 + iTime * 0.03));

    float hoopTop = smoothstep(0.05, 0.0, abs(uv.y - 0.85));
    float hoopBot = smoothstep(0.05, 0.0, abs(uv.y - 0.15));
    float hoop    = max(hoopTop, hoopBot);

    float drip = smoothstep(
        0.02,
        0.0,
        fract(uv.y * 3.0 - iTime * 0.25 + 0.3 * FBM2(uv * 6.0))
    );

    vec3 woodDark  = vec3(0.32, 0.16, 0.06);
    vec3 woodLight = vec3(0.62, 0.36, 0.16);

    vec3 albedo = mix(woodDark, woodLight, grain);
    albedo *= mix(0.75, 1.0, stave);
    albedo  = mix(albedo, vec3(0.15, 0.12, 0.10), hoop * 0.85);
    albedo += drip * vec3(0.08, 0.08, 0.06);

    float height = grain * 0.5 + stave * 0.35 + hoop * 0.5;

    fragColor = vec4(albedo, height);
}

void main() {
    mainImage(outColor, gl_FragCoord.xy);
}
