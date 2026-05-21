#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outOcclusion;

layout(set = 0, binding = 0) uniform sampler2D depthTexture;
layout(set = 0, binding = 1) uniform sampler2D normalsTexture;
layout(set = 0, binding = 2) uniform sampler2D noiseTexture;

layout(push_constant) uniform SsaoPushConstants {
    mat4 projection;
    mat4 invProjection;
    vec2 noiseScale;
    float radius;
    float bias;
    int kernelSize;
    float power;
    float padding1;
    float padding2;
} pc;

layout(set = 0, binding = 3) uniform SsaoKernel {
    vec4 samples[64];
} kernel;

vec3 viewPosFromDepth(vec2 uv, float depth) {
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = pc.invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main() {
    float depth = texture(depthTexture, inUV).r;

    if (depth >= 1.0) {
        outOcclusion = 1.0;
        return;
    }

    vec3 fragPos = viewPosFromDepth(inUV, depth);
    vec3 normal = texture(normalsTexture, inUV).rgb * 2.0 - 1.0;
    normal = normalize(normal);
    vec3 randomVec = normalize(texture(noiseTexture, inUV * pc.noiseScale).xyz * 2.0 - 1.0);
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    float occlusion = 0.0;
    int sampleCount = min(pc.kernelSize, 64);

    for (int i = 0; i < sampleCount; ++i) {
        vec3 sampleDir = TBN * kernel.samples[i].xyz;
        vec3 samplePos = fragPos + sampleDir * pc.radius;

        vec4 offset = pc.projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;

        float sampleDepth = texture(depthTexture, offset.xy).r;
        vec3 sampleViewPos = viewPosFromDepth(offset.xy, sampleDepth);

        float rangeCheck = smoothstep(0.0, 1.0, pc.radius / abs(fragPos.z - sampleViewPos.z));

        occlusion += (sampleViewPos.z >= samplePos.z + pc.bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(sampleCount));
    outOcclusion = pow(occlusion, pc.power);
}
