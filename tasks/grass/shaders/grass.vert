#version 450
#extension GL_ARB_separate_shader_objects : enable

const int kSegments     = 3;
const int kVertsPerBlade = kSegments * 6;

struct GrassInstance {
    float posX;
    float posY;
    float posZ;
    float rotation;
    float scale;
    float pad0;
    float pad1;
    float pad2;
};

layout(set = 0, binding = 0) readonly buffer GrassInstanceBuf {
    GrassInstance instances[];
};

layout(push_constant) uniform DrawParams {
    mat4  projView;
    float time;
    float windStrength;
    float bladeWidth;
    float bladeHeight;
    vec3  camPos;
    float _pad;
} params;

layout(location = 0) out vec3  outWorldPos;
layout(location = 1) out vec3  outNormal;
layout(location = 2) out float outAmbientOcclusion;
layout(location = 3) out float outTipFactor;

float Hash (vec2 p) {
    p = fract(p * vec2(443.8975, 397.2973));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}

float Fade (float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float ValueNoise (vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = vec2(Fade(f.x), Fade(f.y));
    return mix(
        mix(Hash(i), Hash(i + vec2(1.0, 0.0)), u.x),
        mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

float WindNoise (vec2 uv) {
    float n  = ValueNoise(uv) * 2.0 - 1.0;
    n += ValueNoise(uv * 2.1 + vec2(3.7, 9.1)) * 0.5 * 2.0 - 0.5;
    return n / 1.5;
}


void main ()
{
    int segIdx   = gl_VertexIndex / 6;
    int vtxInSeg = gl_VertexIndex % 6;

    float tBot = float(segIdx)     / float(kSegments);
    float tTop = float(segIdx + 1) / float(kSegments);

    float t, side;

    if      (vtxInSeg == 0) { t = tBot; side = -1.0; }
    else if (vtxInSeg == 1) { t = tBot; side = +1.0; }
    else if (vtxInSeg == 2) { t = tTop; side = -1.0; }
    else if (vtxInSeg == 3) { t = tBot; side = +1.0; }
    else if (vtxInSeg == 4) { t = tTop; side = +1.0; }
    else                    { t = tTop; side = -1.0; }
    GrassInstance inst   = instances[gl_InstanceIndex];
    vec3          base   = vec3(inst.posX, inst.posY, inst.posZ);
    float         rot    = inst.rotation;
    float         scale  = inst.scale;

    float distBlade = distance(base.xz, params.camPos.xz);
    int   activeSeg = kSegments;
    if (distBlade > 150.0) activeSeg = 2;
    if (distBlade > 250.0) activeSeg = 1;

    if (segIdx >= activeSeg) {
        outWorldPos         = vec3(0.0);
        outNormal           = vec3(0.0, 1.0, 0.0);
        outAmbientOcclusion = 0.0;
        outTipFactor        = 0.0;
        gl_Position         = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }

    float height    = params.bladeHeight * scale;
    float halfWidth = params.bladeWidth * 0.5 * (1.0 - t * 0.85);

    float cosR = cos(rot);
    float sinR = sin(rot);
    vec3  bladeRight = vec3(cosR, 0.0, -sinR);
    vec3  bladeUp    = normalize(vec3(sinR * 0.25 * t, 1.0, cosR * 0.25 * t));

    vec2 windUV = base.xz * 0.04;
    float windX = WindNoise(windUV + vec2(params.time * 0.60,  0.0));
    float windZ = WindNoise(windUV + vec2(0.0, params.time * 0.45) + vec2(17.3, 31.7));

    vec2  windDir2 = normalize(vec2(windX, windZ) + vec2(0.001));
    float windMag  = length(vec2(windX, windZ)) * params.windStrength;
    vec3  windDisp = vec3(windDir2.x, 0.0, windDir2.y) * windMag;

    float windFactor = t * t;

    vec3 pos = base
             + bladeUp    * (height * t)
             + bladeRight * (halfWidth * side)
             + windDisp   * windFactor;

    vec3 rawNormal   = normalize(cross(bladeRight, bladeUp));
    float edgeFade   = abs(side);
    vec3  wrapNormal = normalize(mix(rawNormal, bladeRight * side, edgeFade * 0.4));

    outWorldPos         = pos;
    outNormal           = wrapNormal;
    outAmbientOcclusion = t;
    outTipFactor        = t;

    gl_Position = params.projView * vec4(pos, 1.0);
}
