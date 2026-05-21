#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outOcclusion;

layout(set = 0, binding = 0) uniform sampler2D ssaoTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform BlurPushConstants {
    vec2 texelSize;
    int blurSize;
    float depthThreshold;
} pc;

void main() {
    float centerDepth = texture(depthTexture, inUV).r;
    float centerAO = texture(ssaoTexture, inUV).r;

    float result = 0.0;
    float totalWeight = 0.0;

    for (int x = -pc.blurSize; x <= pc.blurSize; ++x) {
        for (int y = -pc.blurSize; y <= pc.blurSize; ++y) {
            vec2 offset = vec2(float(x), float(y)) * pc.texelSize;
            vec2 sampleUV = inUV + offset;

            float sampleAO = texture(ssaoTexture, sampleUV).r;
            float sampleDepth = texture(depthTexture, sampleUV).r;

            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff / pc.depthThreshold);

            float spatialWeight = 1.0;

            float weight = depthWeight * spatialWeight;
            result += sampleAO * weight;
            totalWeight += weight;
        }
    }

    outOcclusion = result / max(totalWeight, 0.001);
}
