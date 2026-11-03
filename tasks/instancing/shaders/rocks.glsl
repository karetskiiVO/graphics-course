layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    vec4 camera;
    vec4 material;
} pc;
