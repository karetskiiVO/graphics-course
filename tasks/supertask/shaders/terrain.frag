#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 cameraPos;
    float heightScale;
    vec2 chunkOffset;
    float chunkSize;
    float tessellationFactor;
    float terrainSize;
    float detailTiling;
    float useClipmap;
    float clipmapBaseSize;
    float clipmapCascadeCount;
    float clipmapCenterX;
    float clipmapCenterZ;
    float padding;
} pc;

layout(set = 0, binding = 0) uniform sampler2D heightMap;
layout(set = 0, binding = 1) uniform sampler2D splatMap;
layout(set = 0, binding = 2) uniform sampler2DArray detailTextures;
layout(set = 0, binding = 3) uniform sampler2DArray detailNormals;

layout(set = 0, binding = 4) uniform sampler2D clipmapCascade0;
layout(set = 0, binding = 5) uniform sampler2D clipmapCascade1;
layout(set = 0, binding = 6) uniform sampler2D clipmapCascade2;
layout(set = 0, binding = 7) uniform sampler2D clipmapCascade3;

vec4 heightBlend(
    vec4 color0, float height0, float weight0,
    vec4 color1, float height1, float weight1,
    vec4 color2, float height2, float weight2,
    vec4 color3, float height3, float weight3
) {
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

vec3 blendNormals(vec3 terrainNormal, vec3 detailNormal) {
    vec3 dn = detailNormal * 2.0 - 1.0;

    vec3 t = terrainNormal;
    vec3 u = dn;

    vec3 r = normalize(vec3(
        t.x + u.x,
        t.y,
        t.z + u.z
    ));

    return r;
}

vec4 sampleClipmapCascade(int cascade, vec2 worldXZ) {
    float cascadeSize = pc.clipmapBaseSize * pow(2.0, float(cascade));
    vec2 center = vec2(pc.clipmapCenterX, pc.clipmapCenterZ);
    vec2 uv = (worldXZ - center) / cascadeSize + 0.5;

    if (uv.x < 0.01 || uv.x > 0.99 || uv.y < 0.01 || uv.y > 0.99) {
        return vec4(-1.0);
    }

    if (cascade == 0) return texture(clipmapCascade0, uv);
    if (cascade == 1) return texture(clipmapCascade1, uv);
    if (cascade == 2) return texture(clipmapCascade2, uv);
    return texture(clipmapCascade3, uv);
}

vec4 doSplatting(vec2 uv, vec2 detailUV) {
    vec4 splat = texture(splatMap, uv);
    float wGravel = splat.r;
    float wGrass  = splat.g;
    float wRock   = splat.b;
    float wSnow   = splat.a;

    vec4 gravel = texture(detailTextures, vec3(detailUV, 0.0));
    vec4 grass  = texture(detailTextures, vec3(detailUV, 1.0));
    vec4 rock   = texture(detailTextures, vec3(detailUV, 2.0));
    vec4 snow   = texture(detailTextures, vec3(detailUV, 3.0));

    return heightBlend(
        vec4(gravel.rgb, 1.0), gravel.a, wGravel,
        vec4(grass.rgb, 1.0),  grass.a,  wGrass,
        vec4(rock.rgb, 1.0),   rock.a,   wRock,
        vec4(snow.rgb, 1.0),   snow.a,   wSnow
    );
}

vec3 getDetailNormal(vec2 detailUV, vec4 splat) {
    vec3 nGravel = texture(detailNormals, vec3(detailUV, 0.0)).rgb;
    vec3 nGrass  = texture(detailNormals, vec3(detailUV, 1.0)).rgb;
    vec3 nRock   = texture(detailNormals, vec3(detailUV, 2.0)).rgb;
    vec3 nSnow   = texture(detailNormals, vec3(detailUV, 3.0)).rgb;

    vec3 blended = nGravel * splat.r + nGrass * splat.g + nRock * splat.b + nSnow * splat.a;
    float total = splat.r + splat.g + splat.b + splat.a;
    if (total > 0.001) blended /= total;
    else blended = vec3(0.5, 1.0, 0.5);

    return blended;
}

void main () {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(inNormal);

    vec2 detailUV = inWorldPos.xz / pc.terrainSize * pc.detailTiling;

    vec4 baseColor;

    if (pc.useClipmap > 0.5) {
        vec2 worldXZ = inWorldPos.xz;
        vec4 clipmapColor = vec4(-1.0);

        int cascadeCount = int(pc.clipmapCascadeCount);
        for (int i = 0; i < cascadeCount; ++i) {
            clipmapColor = sampleClipmapCascade(i, worldXZ);
            if (clipmapColor.r >= 0.0) break;
        }

        baseColor = clipmapColor.r < 0.0 ? doSplatting(inUV, detailUV) : clipmapColor;
    } else {
        baseColor = doSplatting(inUV, detailUV);
    }

    vec4 splat = texture(splatMap, inUV);
    vec3 detailNorm = getDetailNormal(detailUV, splat);
    vec3 finalNormal = blendNormals(normal, detailNorm);
    finalNormal = normalize(finalNormal);

    float diffuse = max(dot(finalNormal, lightDir), 0.0);
    float ambient = 0.3;

    vec3 viewDir = normalize(pc.cameraPos - inWorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(finalNormal, halfDir), 0.0), 32.0) * 0.15;

    vec3 finalColor = baseColor.rgb * (ambient + diffuse * 0.7) + vec3(spec);

    outColor = vec4(finalColor, 1.0);
}
