#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters : enable

#include "unpack_attributes.glsl"

layout(location = 0) in vec4 vPosAndNorm;
layout(location = 1) in vec4 vTexCoordAndTangentAndPad;

struct DrawElementInfo {
    mat4 model;
    uint textureIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

layout(set = 0, binding = 0) readonly buffer DrawInfoBuf
{
    DrawElementInfo drawInfos[];
};

layout(push_constant) uniform params_t
{
    mat4 projView;
} params;

layout(location = 0) out VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec2 texCoord;
    flat uint textureIndex;
} vOut;

out gl_PerVertex { vec4 gl_Position; };

void main() {
    uint drawId = gl_DrawIDARB;

    vec3 pos = vPosAndNorm.xyz;
    vec3 normal = decode_normal(floatBitsToUint(vPosAndNorm.w));
    vec2 uv = vTexCoordAndTangentAndPad.xy;

    mat4 model = drawInfos[drawId].model;

    vOut.wPos = (model * vec4(pos, 1.0)).xyz;
    vOut.wNorm = normalize(mat3(transpose(inverse(model))) * normal);
    vOut.texCoord = uv;
    vOut.textureIndex = drawInfos[drawId].textureIndex;

    gl_Position = params.projView * vec4(vOut.wPos, 1.0);
}
