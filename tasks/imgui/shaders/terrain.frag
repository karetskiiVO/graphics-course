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
} pc;

void main () {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(inNormal);
    float diffuse = max(dot(normal, lightDir), 0.0);
    float ambient = 0.3;
    vec3 baseColor;
    float height = inWorldPos.y / pc.heightScale;

    if (height < 0.3) {
        baseColor = mix(vec3(0.2, 0.4, 0.1), vec3(0.3, 0.6, 0.2), height / 0.3);
    } else if (height < 0.6) {
        baseColor = mix(vec3(0.3, 0.6, 0.2), vec3(0.5, 0.5, 0.3), (height - 0.3) / 0.3);
    }
    else if (height < 0.8) {
        baseColor = mix(vec3(0.5, 0.5, 0.3), vec3(0.6, 0.5, 0.4), (height - 0.6) / 0.2);
    } else {
        baseColor = mix(vec3(0.6, 0.5, 0.4), vec3(0.9, 0.9, 0.9), (height - 0.8) / 0.2);
    }

    vec3 finalColor = baseColor * (ambient + diffuse * 0.7);

    outColor = vec4(finalColor, 1.0);
}
