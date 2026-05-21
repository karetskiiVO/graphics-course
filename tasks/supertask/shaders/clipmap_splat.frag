#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec2 inWorldXZ;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 centerWorldPos;
    float cascadeWorldSize;
    float terrainSize;
    float detailTiling;
    float heightScale;
    float padding1;
    float padding2;
} pc;

layout(set = 0, binding = 0) uniform sampler2D splatMap;
layout(set = 0, binding = 1) uniform sampler2DArray detailTextures;
layout(set = 0, binding = 2) uniform sampler2DArray detailNormals;
layout(set = 0, binding = 3) uniform sampler2D heightMap;

vec4 heightBlend(vec4 color0, float height0, float weight0,
                 vec4 color1, float height1, float weight1,
                 vec4 color2, float height2, float weight2,
                 vec4 color3, float height3, float weight3) {
    float h0 = height0 * weight0;
    float h1 = height1 * weight1;
    float h2 = height2 * weight2;
    float h3 = height3 * weight3;

    float maxH = max(max(h0, h1), max(h2, h3));
    float blendDepth = 0.2;
    float threshold = maxH - blendDepth;

    float b0 = max(h0 - threshold, 0.0);
    float b1 = max(h1 - threshold, 0.0);
    float b2 = max(h2 - threshold, 0.0);
    float b3 = max(h3 - threshold, 0.0);

    float totalBlend = b0 + b1 + b2 + b3;
    if (totalBlend < 0.001) {
        totalBlend = 1.0;
        b0 = weight0;
        b1 = weight1;
        b2 = weight2;
        b3 = weight3;
    }

    return (color0 * b0 + color1 * b1 + color2 * b2 + color3 * b3) / totalBlend;
}

void main() {
    vec2 terrainUV = inWorldXZ / pc.terrainSize + 0.5;

    if (terrainUV.x < 0.0 || terrainUV.x > 1.0 || terrainUV.y < 0.0 || terrainUV.y > 1.0) {
        outColor = vec4(0.5, 0.6, 0.7, 1.0); // Sky/fog color for out-of-bounds
        return;
    }

    vec4 splat = texture(splatMap, terrainUV);
    float wGravel = splat.r;
    float wGrass  = splat.g;
    float wRock   = splat.b;
    float wSnow   = splat.a;

    vec2 detailUV = inWorldXZ / pc.terrainSize * pc.detailTiling;

    vec4 gravel = texture(detailTextures, vec3(detailUV, 0.0));
    vec4 grass  = texture(detailTextures, vec3(detailUV, 1.0));
    vec4 rock   = texture(detailTextures, vec3(detailUV, 2.0));
    vec4 snow   = texture(detailTextures, vec3(detailUV, 3.0));

    outColor = heightBlend(
        vec4(gravel.rgb, 1.0), gravel.a, wGravel,
        vec4(grass.rgb, 1.0),  grass.a,  wGrass,
        vec4(rock.rgb, 1.0),   rock.a,   wRock,
        vec4(snow.rgb, 1.0),   snow.a,   wSnow
    );
}
