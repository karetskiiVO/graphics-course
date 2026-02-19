#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(vertices = 4) out;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 cameraPos;
    float heightScale;
    vec2 chunkOffset;
    float chunkSize;
    float tessellationFactor;
} pc;

layout(location = 0) in vec4 inPosition[];
layout(location = 0) out vec4 outPosition[];

float calcTessLevel (float dist, float baseFactor) {
    float maxDist = 500.0;
    float minDist = 50.0;

    float t = clamp((dist - minDist) / (maxDist - minDist), 0, 1.0);
    float factor = 1.0 - (t * t);

    float level = baseFactor * factor;
    return max(8, level);
}

void main () {
    outPosition[gl_InvocationID] = inPosition[gl_InvocationID];

    if (gl_InvocationID == 0) {
        vec3 center = vec3(0.0);
        for (int i = 0; i < 4; ++i)
        center += inPosition[i].xyz;
        center /= 4.0;

        float distance = length(pc.cameraPos - center);

        vec3 edge0Center = (inPosition[0].xyz + inPosition[1].xyz) / 2.0;
        vec3 edge1Center = (inPosition[0].xyz + inPosition[2].xyz) / 2.0;
        vec3 edge2Center = (inPosition[1].xyz + inPosition[3].xyz) / 2.0;
        vec3 edge3Center = (inPosition[2].xyz + inPosition[3].xyz) / 2.0;

        float dist0 = length(pc.cameraPos - edge0Center);
        float dist1 = length(pc.cameraPos - edge1Center);
        float dist2 = length(pc.cameraPos - edge2Center);
        float dist3 = length(pc.cameraPos - edge3Center);

        gl_TessLevelOuter[0] = calcTessLevel(dist1, pc.tessellationFactor);
        gl_TessLevelOuter[1] = calcTessLevel(dist0, pc.tessellationFactor);
        gl_TessLevelOuter[2] = calcTessLevel(dist2, pc.tessellationFactor);
        gl_TessLevelOuter[3] = calcTessLevel(dist3, pc.tessellationFactor);

        gl_TessLevelInner[0] = (gl_TessLevelOuter[0] + gl_TessLevelOuter[2]) / 2.0;
        gl_TessLevelInner[1] = (gl_TessLevelOuter[1] + gl_TessLevelOuter[3]) / 2.0;
    }
}
