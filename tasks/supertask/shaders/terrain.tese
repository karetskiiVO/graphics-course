#version 450
#extension GL_ARB_separate_shader_objects : enable


layout(quads, fractional_odd_spacing, ccw) in;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 cameraPos;
    float heightScale;
    vec2 chunkOffset;
    float chunkSize;
    float tessellationFactor;
} pc;

layout(location = 0) in vec4 inPosition[];

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;

layout(set = 0, binding = 0) uniform sampler2D heightMap;

void main () {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec3 p0 = mix(inPosition[0].xyz, inPosition[1].xyz, u);
    vec3 p1 = mix(inPosition[2].xyz, inPosition[3].xyz, u);
    vec3 worldPos = mix(p0, p1, v);

    float terrainSize = pc.chunkSize * 8.0;
    
    vec2 uv = worldPos.xz / terrainSize + 0.5;
    outUV = uv;

    float height = texture(heightMap, uv).r;
    worldPos.y = height * pc.heightScale;

    outWorldPos = worldPos;

    float texelSize = 1.0 / 4096.0;
    float heightL = texture(heightMap, uv + vec2(-texelSize, 0.0)).r * pc.heightScale;
    float heightR = texture(heightMap, uv + vec2(texelSize, 0.0)).r * pc.heightScale;
    float heightD = texture(heightMap, uv + vec2(0.0, -texelSize)).r * pc.heightScale;
    float heightU = texture(heightMap, uv + vec2(0.0, texelSize)).r * pc.heightScale;

    vec3 tangentX = normalize(vec3(2.0 * texelSize * terrainSize, heightR - heightL, 0.0));
    vec3 tangentZ = normalize(vec3(0.0, heightU - heightD, 2.0 * texelSize * terrainSize));

    outNormal = normalize(cross(tangentZ, tangentX));

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
}
