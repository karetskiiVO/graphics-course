#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D ssaoTexture;

layout(push_constant) uniform ApplyPushConstants {
    float ssaoStrength;
    float padding1;
    float padding2;
    float padding3;
} pc;

void main() {
    vec3 color = texture(sceneColor, inUV).rgb;
    float ao = texture(ssaoTexture, inUV).r;

    ao = mix(1.0, ao, pc.ssaoStrength);
    outColor = vec4(color * ao, 1.0);
}
