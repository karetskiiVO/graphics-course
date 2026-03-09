#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3  inWorldPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in float inAmbientOcclusion;
layout(location = 3) in float inTipFactor;

layout(location = 0) out vec4 outColor;

void main () {
    vec3 lightDir   = normalize(vec3(0.5, 1.0, 0.3));
    vec3 lightColor = vec3(1.00, 0.95, 0.80);
    vec3 skyColor   = vec3(0.40, 0.70, 1.00);

    vec3 baseColor = vec3(0.07, 0.28, 0.04);
    vec3 tipColor  = vec3(0.35, 0.65, 0.12);
    vec3 albedo    = mix(baseColor, tipColor, inTipFactor);

    float ao = 0.20 + 0.80 * inAmbientOcclusion;

    vec3  N          = normalize(inNormal);
    float diffFront  = max(dot(N,  lightDir), 0.0);
    float diffBack   = max(dot(-N, lightDir), 0.0) * 0.35;
    float diff       = diffFront + diffBack;

    float sky = 0.5 + 0.5 * dot(N, vec3(0.0, 1.0, 0.0));
    vec3  ambient = mix(vec3(0.05, 0.10, 0.02), skyColor * 0.12, sky);

    vec3 color = albedo * (diff * lightColor + ambient) * ao;

    outColor = vec4(color, 1.0);
}
