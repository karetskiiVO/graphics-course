#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 cameraPos;
    float heightScale;
    vec2 chunkOffset;
    float chunkSize;
    float tessellationFactor;
} pc;

layout(location = 0) out vec4 outPosition;

void main() {
    vec2 positions[4] = vec2[](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0)
    );

    vec2 localPos = positions[gl_VertexIndex];

    vec3 worldPos = vec3(
        pc.chunkOffset.x + localPos.x * pc.chunkSize,
        0.0,
        pc.chunkOffset.y + localPos.y * pc.chunkSize
    );

    outPosition = vec4(worldPos, 1.0);
}
