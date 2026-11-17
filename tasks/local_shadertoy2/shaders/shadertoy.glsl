layout(push_constant) uniform params {
    uint resolutionX;
    uint resolutionY;
    float mouseX;
    float mouseY;
    float time;
} env;

#define iResolution vec3(float(env.resolutionX), float(env.resolutionY), 1.0)
#define iTime env.time
#define iMouse vec4(env.mouseX, float(env.resolutionY) - env.mouseY, 0.0, 0.0)

layout(location = 0) out vec4 outColor;
