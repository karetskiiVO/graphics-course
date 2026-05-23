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

bool isUvInside(vec2 uv) {
    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
}

void main() {
    float centerDepth = texture(depthTexture, inUV).r;
    float centerAO = texture(ssaoTexture, inUV).r;

    if (centerDepth >= 0.9999) {
        outOcclusion = 1.0;
        return;
    }

    float result = 0.0;
    float totalWeight = 0.0;
    float sigma = max(float(pc.blurSize), 1.0) * 0.5;
    float depthThreshold = max(pc.depthThreshold, 1e-6);

    for (int x = -pc.blurSize; x <= pc.blurSize; ++x) {
        for (int y = -pc.blurSize; y <= pc.blurSize; ++y) {
            vec2 pixelOffset = vec2(float(x), float(y));
            vec2 sampleUV = inUV + pixelOffset * pc.texelSize;

            if (!isUvInside(sampleUV)) {
                continue;
            }

            float sampleDepth = texture(depthTexture, sampleUV).r;
            if (sampleDepth >= 0.9999) {
                continue;
            }

            float sampleAO = texture(ssaoTexture, sampleUV).r;
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff / depthThreshold);
            float spatialWeight = exp(-dot(pixelOffset, pixelOffset) / (2.0 * sigma * sigma));

            float weight = depthWeight * spatialWeight;
            result += sampleAO * weight;
            totalWeight += weight;
        }
    }

    outOcclusion = totalWeight > 0.0 ? result / totalWeight : centerAO;
}
