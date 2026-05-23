#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform FxaaPushConstants {
    vec2 inverseScreenSize;
    int aaMode;
    float subpixelQuality;
    float edgeThreshold;
    float edgeThresholdMin;
    float padding1;
    float padding2;
} pc;

float luminance(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec4 fxaaBasic(vec2 uv) {
    vec2 texelSize = pc.inverseScreenSize;

    vec3 colorCenter = texture(inputTexture, uv).rgb;
    vec3 colorN = texture(inputTexture, uv + vec2( 0.0, -texelSize.y)).rgb;
    vec3 colorS = texture(inputTexture, uv + vec2( 0.0,  texelSize.y)).rgb;
    vec3 colorE = texture(inputTexture, uv + vec2( texelSize.x,  0.0)).rgb;
    vec3 colorW = texture(inputTexture, uv + vec2(-texelSize.x,  0.0)).rgb;

    float lumCenter = luminance(colorCenter);
    float lumN = luminance(colorN);
    float lumS = luminance(colorS);
    float lumE = luminance(colorE);
    float lumW = luminance(colorW);

    float lumMin = min(lumCenter, min(min(lumN, lumS), min(lumE, lumW)));
    float lumMax = max(lumCenter, max(max(lumN, lumS), max(lumE, lumW)));

    float lumRange = lumMax - lumMin;

    if (lumRange < max(pc.edgeThresholdMin, lumMax * pc.edgeThreshold)) return vec4(colorCenter, 1.0);

    vec3 colorNW = texture(inputTexture, uv + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 colorNE = texture(inputTexture, uv + vec2( texelSize.x, -texelSize.y)).rgb;
    vec3 colorSW = texture(inputTexture, uv + vec2(-texelSize.x,  texelSize.y)).rgb;
    vec3 colorSE = texture(inputTexture, uv + vec2( texelSize.x,  texelSize.y)).rgb;

    float lumNW = luminance(colorNW);
    float lumNE = luminance(colorNE);
    float lumSW = luminance(colorSW);
    float lumSE = luminance(colorSE);

    float lumAvg = (lumN + lumS + lumE + lumW) * 0.25;
    float subpixelOffset = abs(lumAvg - lumCenter);
    subpixelOffset = clamp(subpixelOffset / lumRange, 0.0, 1.0);
    subpixelOffset = smoothstep(0.0, 1.0, subpixelOffset);
    subpixelOffset = subpixelOffset * subpixelOffset * pc.subpixelQuality;

    float edgeH =
        abs(-2.0 * lumN + lumNW + lumNE) +
        abs(-2.0 * lumCenter + lumW + lumE) * 2.0 +
        abs(-2.0 * lumS + lumSW + lumSE);

    float edgeV =
        abs(-2.0 * lumW + lumNW + lumSW) +
        abs(-2.0 * lumCenter + lumN + lumS) * 2.0 +
        abs(-2.0 * lumE + lumNE + lumSE);

    bool isHorizontal = (edgeH >= edgeV);

    float stepLength = isHorizontal ? texelSize.y : texelSize.x;

    float lumPos, lumNeg;
    if (isHorizontal) {
        lumPos = lumS;
        lumNeg = lumN;
    } else {
        lumPos = lumE;
        lumNeg = lumW;
    }

    float gradientPos = abs(lumPos - lumCenter);
    float gradientNeg = abs(lumNeg - lumCenter);

    if (gradientNeg > gradientPos) stepLength = -stepLength;

    vec2 currentUV = uv;
    isHorizontal ? currentUV.y += stepLength * 0.5 : currentUV.x += stepLength * 0.5;

    vec2 searchDir = isHorizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);

    float lumLocalAvg = 0.5 * (lumCenter + (gradientNeg > gradientPos ? lumNeg : lumPos));
    float gradientScaled = 0.25 * max(gradientPos, gradientNeg);

    vec2 uvP = currentUV + searchDir;
    float lumEndP = luminance(texture(inputTexture, uvP).rgb);
    lumEndP -= lumLocalAvg;

    vec2 uvN = currentUV - searchDir;
    float lumEndN = luminance(texture(inputTexture, uvN).rgb);
    lumEndN -= lumLocalAvg;

    bool reachedP = abs(lumEndP) >= gradientScaled;
    bool reachedN = abs(lumEndN) >= gradientScaled;

    for (int i = 0; i < 8; i++) {
        if (!reachedP) {
            uvP += searchDir;
            lumEndP = luminance(texture(inputTexture, uvP).rgb) - lumLocalAvg;
            reachedP = abs(lumEndP) >= gradientScaled;
        }
        if (!reachedN) {
            uvN -= searchDir;
            lumEndN = luminance(texture(inputTexture, uvN).rgb) - lumLocalAvg;
            reachedN = abs(lumEndN) >= gradientScaled;
        }
        if (reachedP && reachedN) break;
    }

    float distP, distN;
    if (isHorizontal) {
        distP = uvP.x - uv.x;
        distN = uv.x - uvN.x;
    } else {
        distP = uvP.y - uv.y;
        distN = uv.y - uvN.y;
    }

    float totalDist = distP + distN;
    float edgeBlend;

    bool isCloserToNeg = (distN < distP);
    float lumEnd = isCloserToNeg ? lumEndN : lumEndP;
    float closerDist = min(distP, distN);

    edgeBlend = ((lumCenter - lumLocalAvg) < 0.0) == (lumEnd < 0.0) ? 0 : 0.5 - closerDist / totalDist;

    float finalOffset = max(subpixelOffset, edgeBlend);

    vec2 finalUV = uv;
    if (isHorizontal) {
        finalUV.y += finalOffset * stepLength;
    } else {
        finalUV.x += finalOffset * stepLength;
    }

    return vec4(texture(inputTexture, finalUV).rgb, 1.0);
}

#define FXAA_QUALITY_STEPS 12
const float qualitySteps[FXAA_QUALITY_STEPS] = float[](
    1.0, 1.0, 1.0, 1.0, 1.0,
    1.5, 2.0, 2.0, 2.0, 2.0,
    4.0, 8.0
);

vec4 fxaa311(vec2 uv) {
    vec2 texelSize = pc.inverseScreenSize;

    vec3 rgbM  = texture(inputTexture, uv).rgb;
    vec3 rgbN  = texture(inputTexture, uv + vec2( 0.0, -texelSize.y)).rgb;
    vec3 rgbS  = texture(inputTexture, uv + vec2( 0.0,  texelSize.y)).rgb;
    vec3 rgbE  = texture(inputTexture, uv + vec2( texelSize.x,  0.0)).rgb;
    vec3 rgbW  = texture(inputTexture, uv + vec2(-texelSize.x,  0.0)).rgb;

    float lumM = luminance(rgbM);
    float lumN = luminance(rgbN);
    float lumS = luminance(rgbS);
    float lumE = luminance(rgbE);
    float lumW = luminance(rgbW);

    float rangeMin = min(lumM, min(min(lumN, lumS), min(lumE, lumW)));
    float rangeMax = max(lumM, max(max(lumN, lumS), max(lumE, lumW)));
    float range = rangeMax - rangeMin;

    if (range < max(pc.edgeThresholdMin, rangeMax * pc.edgeThreshold)) return vec4(rgbM, 1.0);

    vec3 rgbNW = texture(inputTexture, uv + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 rgbNE = texture(inputTexture, uv + vec2( texelSize.x, -texelSize.y)).rgb;
    vec3 rgbSW = texture(inputTexture, uv + vec2(-texelSize.x,  texelSize.y)).rgb;
    vec3 rgbSE = texture(inputTexture, uv + vec2( texelSize.x,  texelSize.y)).rgb;

    float lumNW = luminance(rgbNW);
    float lumNE = luminance(rgbNE);
    float lumSW = luminance(rgbSW);
    float lumSE = luminance(rgbSE);

    float lumNS = lumN + lumS;
    float lumWE = lumW + lumE;
    float lumCorners = lumNW + lumNE + lumSW + lumSE;

    float subpixNSWE = lumNS + lumWE;
    float subpixAll = subpixNSWE + lumCorners;
    float subpixAvg = subpixAll * (1.0 / 12.0) + lumM * (1.0 / 6.0);

    subpixAvg = (2.0 * subpixNSWE + lumCorners) / 12.0;
    float subpixOffset = clamp(abs(subpixAvg - lumM) / range, 0.0, 1.0);
    subpixOffset = smoothstep(0.0, 1.0, subpixOffset);
    subpixOffset = subpixOffset * subpixOffset * pc.subpixelQuality;

    float edgeHorz =
        abs(-2.0 * lumN + lumNW + lumNE) +
        abs(-2.0 * lumM + lumW  + lumE ) * 2.0 +
        abs(-2.0 * lumS + lumSW + lumSE);

    float edgeVert =
        abs(-2.0 * lumW + lumNW + lumSW) +
        abs(-2.0 * lumM + lumN  + lumS ) * 2.0 +
        abs(-2.0 * lumE + lumNE + lumSE);

    bool horzSpan = (edgeHorz >= edgeVert);

    float lengthSign = -(horzSpan ? texelSize.y : texelSize.x);

    float lumNeg = horzSpan ? lumN : lumW;
    float lumPos = horzSpan ? lumS : lumE;

    float gradientNeg = abs(lumNeg - lumM);
    float gradientPos = abs(lumPos - lumM);

    bool pairN = (gradientNeg >= gradientPos);

    vec2 posM = uv;
    if (horzSpan) {
        posM.y += lengthSign * 0.5;
    } else {
        posM.x += lengthSign * 0.5;
    }

    vec2 offNP = horzSpan ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);

    vec2 posP = posM + offNP * qualitySteps[0];
    vec2 posN = posM - offNP * qualitySteps[0];

    float lumaEndN, lumaEndP;
    float gradientScaled = max(gradientNeg, gradientPos) * 0.25;
    float lumMM = lumM + (pairN ? lumNeg : lumPos);
    lumMM *= 0.5;

    lumaEndN = luminance(texture(inputTexture, posN).rgb) - lumMM;
    lumaEndP = luminance(texture(inputTexture, posP).rgb) - lumMM;

    bool doneN = abs(lumaEndN) >= gradientScaled;
    bool doneP = abs(lumaEndP) >= gradientScaled;

    for (int i = 1; i < FXAA_QUALITY_STEPS; i++) {
        if (!doneN) {
            posN -= offNP * qualitySteps[i];
            lumaEndN = luminance(texture(inputTexture, posN).rgb) - lumMM;
            doneN = abs(lumaEndN) >= gradientScaled;
        }
        if (!doneP) {
            posP += offNP * qualitySteps[i];
            lumaEndP = luminance(texture(inputTexture, posP).rgb) - lumMM;
            doneP = abs(lumaEndP) >= gradientScaled;
        }
        if (doneN && doneP) break;
    }

    float dstN, dstP;
    if (horzSpan) {
        dstN = uv.x - posN.x;
        dstP = posP.x - uv.x;
    } else {
        dstN = uv.y - posN.y;
        dstP = posP.y - uv.y;
    }

    float spanLength = dstN + dstP;
    bool directionN = (dstN < dstP);
    float dst = min(dstN, dstP);
    float lumaEnd = directionN ? lumaEndN : lumaEndP;

    bool goodSpan = ((lumaEnd < 0.0) != ((lumM - lumMM) < 0.0));

    float pixelOffset;
    if (goodSpan) {
        pixelOffset = 0.5 - dst / spanLength;
    } else {
        pixelOffset = 0.0;
    }

    float finalOffset = max(pixelOffset, subpixOffset);

    vec2 finalUV = uv;
    if (horzSpan) {
        finalUV.y += finalOffset * lengthSign;
    } else {
        finalUV.x += finalOffset * lengthSign;
    }

    return vec4(texture(inputTexture, finalUV).rgb, 1.0);
}

void main() {
    if (pc.aaMode == 0) {
        outColor = texture(inputTexture, inUV);
    } else if (pc.aaMode == 1) {
        outColor = fxaaBasic(inUV);
    } else {
        outColor = fxaa311(inUV);
    }
}
