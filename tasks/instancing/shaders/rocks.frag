#version 450
#extension GL_GOOGLE_include_directive : require

#include "rocks.glsl"

layout(set = 0, binding = 1) uniform sampler2D albedoMap;
layout(set = 0, binding = 2) uniform sampler2D normalMap;
layout(set = 0, binding = 3) uniform sampler2D armMap;
layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 tint;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

vec3 SurfaceNormal() {
    vec3 n = normalize(worldNormal);
    vec3 dx = dFdx(worldPosition);
    vec3 dy = dFdy(worldPosition);
    vec2 du = dFdx(texCoord);
    vec2 dv = dFdy(texCoord);
    float determinant = du.x * dv.y - du.y * dv.x;
    if (abs(determinant) < 1e-10) return n;
    vec3 tangent = (dx * dv.y - dy * du.y) / determinant;
    vec3 bitangent = (-dx * dv.x + dy * du.x) / determinant;
    tangent = normalize(tangent - n * dot(n, tangent));
    float orientation = dot(cross(n, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
    bitangent = cross(n, tangent) * orientation;
    vec3 mapped = texture(normalMap, texCoord).xyz * 2.0 - 1.0;
    mapped.xy *= pc.material.z;
    return normalize(mat3(tangent, bitangent, n) * mapped);
}

float Geometry(float cosine, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return cosine / max(cosine * (1.0 - k) + k, 0.0001);
}

void main() {
    vec3 albedo = texture(albedoMap, texCoord).rgb * tint;
    vec3 arm = texture(armMap, texCoord).rgb;

    float roughness = clamp(arm.g * pc.material.x, 0.08, 1.0);
    float metallic = clamp(arm.b * pc.material.y, 0.0, 1.0);

    vec3 n = SurfaceNormal();
    vec3 v = normalize(pc.camera.xyz - worldPosition);
    vec3 l = normalize(vec3(-0.5, 0.8, -0.3));
    vec3 h = normalize(v + l);

    float nv = max(dot(n, v), 0.0001);
    float nl = max(dot(n, l), 0.0);
    float nh = max(dot(n, h), 0.0);
    float a = roughness * roughness;
    float denominator = nh * nh * (a * a - 1.0) + 1.0;
    float distribution = a * a / max(PI * denominator * denominator, 0.0001);

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    vec3 specular = distribution * Geometry(nv, roughness) * Geometry(nl, roughness) * fresnel / max(4.0 * nv * nl, 0.0001);
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / PI;
    vec3 color = albedo * vec3(0.16, 0.2, 0.25) * arm.r;
    color += (diffuse + specular) * vec3(3.0, 2.76, 2.4) * nl;
    float fog = 1.0 - exp(-distance(pc.camera.xyz, worldPosition) * 0.00065);
    color = mix(color, vec3(0.48, 0.67, 0.85), fog);
    outColor = vec4(pow(max(color, vec3(0)), vec3(1.0 / 2.2)), 1.0);
}
