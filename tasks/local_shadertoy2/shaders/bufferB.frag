#version 450
#extension GL_GOOGLE_include_directive : require

#include "shadertoy.glsl"

const float PI     = 3.14159265359;
const int   STAVES = 12;

float Hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float Noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = Hash(i + vec3(0,0,0));
    float n100 = Hash(i + vec3(1,0,0));
    float n010 = Hash(i + vec3(0,1,0));
    float n110 = Hash(i + vec3(1,1,0));
    float n001 = Hash(i + vec3(0,0,1));
    float n101 = Hash(i + vec3(1,0,1));
    float n011 = Hash(i + vec3(0,1,1));
    float n111 = Hash(i + vec3(1,1,1));

    return mix(
        mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
        mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y),
        f.z
    );
}

float FBM(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * Noise(p);
        p = p * 2.03 + vec3(1.7, 9.2, 3.1);
        a *= 0.5;
    }
    return v / 0.9375;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2  uv  = fragCoord / iResolution.xy;
    float ang = uv.x * 2.0 * PI;
    float y   = (uv.y - 0.5) * 1.4;

    vec2 cs = vec2(cos(ang), sin(ang)) * 0.55;

    float s        = uv.x * float(STAVES);
    float plank    = floor(s);
    float edge     = min(fract(s), 1.0 - fract(s));
    float gap      = 1.0 - smoothstep(0.02, 0.07, edge);
    float plankRnd = Hash(vec3(plank, 3.7, 1.3));

    vec3  gp    = vec3(cs * 14.0, y * 2.5) + plankRnd * 40.0;
    float grain = FBM(gp);
    float fine  = Noise(vec3(cs * 60.0, y * 6.0));

    float woodRough = 
        0.55 + 0.30 * grain +
        0.10 * (plankRnd - 0.5) +
        0.06 * (fine - 0.5);

    float hoopD     = min(abs(y - 0.4), abs(y + 0.4));
    float metalMask = 1.0 - smoothstep(0.020, 0.030, hoopD);

    float rustN   = FBM(vec3(cs * 12.0, y * 12.0) + 11.0);
    float rust    = smoothstep(0.40, 0.68, rustN) * metalMask;
    float scratch = Noise(vec3(cs * 4.0, y * 120.0));

    float metalRough = mix(0.28, 0.80, rust) + 0.12 * (scratch - 0.5);

    gap *= 1.0 - metalMask;

    float roughness = mix(woodRough, metalRough, metalMask);
    roughness       = mix(roughness, 1.0, gap);

    float metallic  = metalMask * (1.0 - 0.85 * rust);
    float cavity    = 1.0 - 0.65 * gap;

    fragColor = vec4(clamp(roughness, 0.04, 1.0), metallic, rust, cavity);
}

void main() {
    mainImage(outColor, gl_FragCoord.xy);
}
