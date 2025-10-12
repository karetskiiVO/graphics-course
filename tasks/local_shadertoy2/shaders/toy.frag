#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D iChannel1;
layout(binding = 1) uniform sampler2D iChannel2;

layout(push_constant) uniform params {
    uint resolutionX;
    uint resolutionY;
    float mouseX;
    float mouseY;
    float time;
} env;

float iTime() {
  return env.time;
}

ivec3 iResolution() {
  return ivec3(env.resolutionX, env.resolutionY, 0);
}

vec3 iMouse() {
  return vec3(env.mouseX, iResolution().y - env.mouseY, 0.0);
}

const float eps = 0.01;

float disttosegment(in vec3 begin, in vec3 end, in vec3 p) {
    vec3 seg = end - begin;
    vec3 beginp = p - begin;
    vec3 endp = p - end;
    
    float t = dot(beginp, seg) / dot(seg, seg);
    
    if (t < 0.0) {
        return length(beginp);
    } else if (t > 1.0) {
        return length(endp);
    }
    
    vec3 projection = begin + t * seg;
    return length(p - projection);
}

float lengthlp(in vec3 v, float p) {
    return pow(
        pow(abs(v.x), p) + pow(abs(v.y), p) + pow(abs(v.z), p),
        1.0 / p
    );
}

const float twoPi = 2.0 * 3.14159265;
float noise(in vec3 p, in mat3 transform) {
    vec3 newp = transform * p.xyx;
    return 0.03 * pow(
        sin(
            20.0 * lengthlp(newp, 12.0) + 
            iTime() * twoPi / 6.0
        ),
        4.0
    );
}

float fig(in vec3 p, float r, in mat3 transform) {
    const int samples = 150;
    const float inf = 100.0;

    const float step = twoPi / float(samples);

    float mindist = inf;
    float t = 0.0;
    vec3 prevp = transform * vec3(
        sin(-2.0 * step), 
        cos(-3.0 * step),
        sin(-1.0 * step)
    );

    for (int smpl = 0; smpl < samples; smpl++) {
        vec3 curvep = transform * vec3(
            sin(2.0 * t), 
            cos(3.0 * t),
            sin(1.0 * t)
        );

        // vec3 seg = curvep - p;
        // mindist = min(
        //     mindist, 
        //     length(seg) - r
        // );

        mindist = min(
            mindist, 
            disttosegment(curvep, prevp, p) - r
        );

        prevp = curvep;
        t += step;
    }

    return mindist + noise(p, transform);
}

float sdf(in vec3 p, in mat3 transform) {
    return fig(p, 0.2, transform);
}

vec3 normal(vec3 z, float d, in mat3 transform) {
    float e   = max(d * 0.5, eps);
    float dx1 = sdf(z + vec3(e, 0, 0), transform);
    float dx2 = sdf(z - vec3(e, 0, 0), transform);
    float dy1 = sdf(z + vec3(0, e, 0), transform);
    float dy2 = sdf(z - vec3(0, e, 0), transform);
    float dz1 = sdf(z + vec3(0, 0, e), transform);
    float dz2 = sdf(z - vec3(0, 0, e), transform);
    
    return normalize(vec3 (dx1 - dx2, dy1 - dy2, dz1 - dz2));
}

const int maxSteps = 65;
const float maxDist = 10.0;

bool raycast(in vec3 from, in vec3 dir, in mat3 transform, out vec3 hit) {
    vec3 p = from;
    float totalDist = 0.0;

    for (int step = 0; step < maxSteps; step++) {
        float dist = sdf(p, transform);

        if (dist < eps) {
            hit = p;
            return true;
        }

        totalDist += dist;
        if (totalDist > maxDist) break;

        p += dist * dir;
    }

    hit = p;
    return false;
}

// Rotation matrix around the X axis.
mat3 rotateX(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(1, 0, 0),
        vec3(0, c, -s),
        vec3(0, s, c)
    );
}

// Rotation matrix around the Y axis.
mat3 rotateY(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(c, 0, s),
        vec3(0, 1, 0),
        vec3(-s, 0, c)
    );
}

// Rotation matrix around the Z axis.
mat3 rotateZ(float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return mat3(
        vec3(c, s, 0),
        vec3(-s, c, 0),
        vec3(0, 0, 1)
    );
}

vec3 triplanarWeights (in vec3 n) {
    vec3 w = abs(n);
    w *= w;

    return w / (w.x + w.y + w.z);
}

vec4 calclight (
    in vec3 materialpoint, 
    in vec3 norm,
    in vec3 light,
    in vec3 view
) {
    vec3 l = normalize(light - materialpoint);
    vec3 v = normalize(view - materialpoint);
    vec3 vl = 0.5 * (l + v);

    float nl = max(0.0, dot(norm, l));
    float bl = pow(max(0.0, dot(vl, norm)), 20.0);

    vec4 cx = texture(iChannel2, materialpoint.yz);
    vec4 cy = texture(iChannel2, materialpoint.zx);
    vec4 cz = texture(iChannel2, materialpoint.xy);

    vec3 tw = triplanarWeights(norm);

    return (0.8 * nl + 0.4 * bl) * (tw.x * cx + tw.y + cy + tw.z * cz);
}

const vec3 eye   = vec3(0, 0, 5);
const vec3 light = vec3(0, 2, 5);

void mainImage(out vec4 fragColor, in vec2 fragCoord) {  
    vec3 mouse = vec3(iMouse().xy/iResolution().xy - 0.5, iMouse().z - 0.5);
    mat3 transform = 
        rotateX(6.0 * mouse.y) * 
        rotateY(6.0 * mouse.x) *
        rotateZ(iTime());

    vec2 scale = 9.0 * iResolution().xy / max(iResolution().x, iResolution().y);
    vec2 uv  = scale * (fragCoord/iResolution().xy - vec2(0.5));
	vec3 dir = normalize(vec3 (uv, 0) - eye);

    vec3 hit = vec3(0, 0, 0);
    vec4 color = vec4(0, 0, 0, 1);

    if (raycast(eye, dir, transform, hit)) {
        vec3 n = normal(hit, 1e-3, transform);

        color = calclight(
            hit,
            n,
            light,
            eye
        );
    } else {
        color = texture(iChannel1, fragCoord);
    }

    fragColor = color;
}

void main() {
    vec2 scale = 3.0 * iResolution().xy / max(iResolution().x, iResolution().y);
    vec2 uv = scale * (gl_FragCoord.xy / iResolution().xy - vec2(0.25, 0.25)) * iResolution().xy;

    mainImage(outColor, uv);
}

