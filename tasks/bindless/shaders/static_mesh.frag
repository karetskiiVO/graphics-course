#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) out vec4 out_fragColor;

layout(location = 0) in VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec2 texCoord;
    flat uint textureIndex;
} surf;

layout(set = 1, binding = 0) uniform sampler2D textures[];

void main() {
    vec3 lightPos = vec3(10, 30, 10);
    vec3 lightColor = vec3(1.0);

    vec3 lightDir = normalize(lightPos - surf.wPos);
    float diff = max(dot(surf.wNorm, lightDir), 0.0);
    float ambient = 0.05;

    vec4 albedo = texture(textures[nonuniformEXT(surf.textureIndex)], surf.texCoord);

    out_fragColor = vec4((diff * lightColor + ambient) * albedo.rgb, 1.0);
}
