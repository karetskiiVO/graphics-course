#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "unpack_attributes.glsl"


layout(location = 0) in vec3 vPos;
layout(location = 1) in ivec3 vNorm;
layout(location = 2) in vec2 vUV;
layout(location = 3) in ivec3 vTangent;

layout(std140, set = 0, binding = 0) readonly buffer instanceMatrices_t {
  mat4 matrices[];
} instanceMatrices;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  mat4 mModel;
} params;


layout (location = 0 ) out VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 texCoord;
} vOut;

out gl_PerVertex { vec4 gl_Position; };

void main(void)
{
  mat4 modelTransform = instanceMatrices.matrices[gl_InstanceIndex];

  const vec4 wNorm = vec4(vNorm, 0.0f);
  const vec4 wTang = vec4(vTangent, 0.0f);

  vOut.wPos   = (modelTransform * vec4(vPos, 1.0f)).xyz;
  vOut.wNorm  = normalize(mat3(transpose(inverse(modelTransform))) * wNorm.xyz);
  vOut.wTangent = normalize(mat3(transpose(inverse(modelTransform))) * wTang.xyz);
  vOut.texCoord = vUV;

  gl_Position   = params.mProjView * vec4(vOut.wPos, 1.0);
}
