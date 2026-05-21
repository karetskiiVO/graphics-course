#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants {
    vec2 centerWorldPos;
    float cascadeWorldSize;
    float terrainSize;
    float detailTiling;
    float heightScale;
    float padding1;
    float padding2;
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec2 outWorldXZ;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 uvs[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    outUV = uvs[gl_VertexIndex];

    float halfSize = pc.cascadeWorldSize * 0.5;
    outWorldXZ = pc.centerWorldPos + (outUV - 0.5) * pc.cascadeWorldSize;
}
