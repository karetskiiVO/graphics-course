#version 450
#extension GL_GOOGLE_include_directive : require

#include "shadertoy.glsl"

layout(set = 0, binding = 0) uniform sampler2D iChannel0;
layout(set = 0, binding = 1) uniform sampler2D iChannel1;

const float eps          = 0.01;
const vec3  light        = vec3(5.0, 6.0, 8.0);
const float BOUND_RADIUS = 1.15;

const float PI        = 3.14159265359;
const vec3  SUN_COLOR = vec3(1.0, 0.93, 0.80) * 4.5;
const vec3  IRON_F0   = vec3(0.56, 0.57, 0.58);
const vec3  RUST_COL  = vec3(0.32, 0.11, 0.04);
const float EXPOSURE  = 1.0;

float Hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float Noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = Hash(i + vec3(0,0,0));
    float n100 = Hash(i + vec3(1,0,0));
    float n010 = Hash(i + vec3(0,1,0));
    float n110 = Hash(i + vec3(1,1,0));
    float n001 = Hash(i + vec3(0,0,1));
    float n101 = Hash(i + vec3(1,0,1));
    float n011 = Hash(i + vec3(0,1,1));
    float n111 = Hash(i + vec3(1,1,1));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);

    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

float FBM(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 2; i++) {
        v += a * Noise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

float EllipsoidSDF(vec3 p, vec3 r) {
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / k1;
}

mat3 RotateX(float theta) {
    float s = sin(theta);
    float c = cos(theta);
    return mat3(
        vec3(1, 0, 0),
        vec3(0, c, s),
        vec3(0, -s, c)
    );
}
mat3 RotateY(float theta) {
    float s = sin(theta);
    float c = cos(theta);
    return mat3(
        vec3(c, 0, -s),
        vec3(0, 1, 0),
        vec3(s, 0, c)
    );
}
mat3 RotateZ(float theta) {
    float s = sin(theta);
    float c = cos(theta);
    return mat3(
        vec3(c, s, 0),
        vec3(-s, c, 0),
        vec3(0, 0, 1)
    );
}

vec3 SkyGradient(vec3 dir) {
    float y = max(dir.y, 0.0);
    vec3 zenith  = vec3(0.15, 0.35, 0.75);
    vec3 horizon = vec3(0.78, 0.86, 0.95);
    vec3 col = mix(horizon, zenith, pow(y, 0.55));
    if (dir.y < 0.0) {
        col = mix(horizon, vec3(0.20, 0.28, 0.38), clamp(-dir.y * 3.0, 0.0, 1.0));
    }
    return pow(col, vec3(2.2));
}

vec3 SkyColor(vec3 dir) {
    vec3 col = SkyGradient(dir);
    if (dir.y >= 0.0) {
        vec3  sunDir = normalize(light);
        float sun = max(dot(dir, sunDir), 0.0);
        col += vec3(1.0, 0.92, 0.72) * pow(sun, 250.0) * 3.5;
        col += vec3(1.0, 0.80, 0.50) * pow(sun, 8.0)   * 0.25;
    }
    return col;
}

struct Transform {
    vec3 position;
    mat3 rotation;
};

struct Barrel {
    Transform transform;
    vec4 color;
};

struct Sea {
    Transform transform;
    vec4 color;
};

struct Material {
    vec3  albedo;
    float roughness;
    float metallic;
    float cavity;
};

const vec2  BARREL_POS[3]   = vec2[3](vec2(-2.4, 0.6), vec2(-0.8, -1.1), vec2(1.0, 0.7));
const float BARREL_PHASE[3] = float[3](0.0, 1.0, 2.0);

struct Scene {
    Sea    sea;
    Barrel barrels[3];
};

vec3 BarrelLocal(in Barrel barrel, vec3 p) {
    return transpose(barrel.transform.rotation) * (p - barrel.transform.position);
}

vec2 BarrelUV(vec3 q) {
    float ang = atan(q.z, q.x);
    float u = ang / 6.2831853 + 0.5;
    float v = clamp(q.y / 0.7 * 0.5 + 0.5, 0.0, 1.0);
    return vec2(u, v);
}

float BarrelSDF(in Barrel barrel, vec3 p, bool detailed) {
    vec3 q = BarrelLocal(barrel, p);

    vec3  r = vec3(0.55, 1.0, 0.55);
    float h = 0.7;

    float dist = EllipsoidSDF(q, r);

    if (detailed) {
        float ang   = atan(q.z, q.x);
        float stave = smoothstep(0.0, 0.2, abs(sin(ang * 6.0))) - 0.5;
        float grain = FBM(vec3(q.xz * 7.0, q.y * 1.5)) - 0.5;

        float ring1 = smoothstep(0.025, 0.0, abs(q.y - 0.4));
        float ring2 = smoothstep(0.025, 0.0, abs(q.y + 0.4));
        float hoop  = max(ring1, ring2);

        dist -= 0.012 * stave + 0.008 * grain + 0.025 * hoop;
    }

    return max(dist, abs(q.y) - h);
}

float WaveHeight(vec3 worldPos) {
    return 0.05 * (FBM(vec3(worldPos.x * 1.5, iTime * 0.4, worldPos.z * 1.5)) - 0.5);
}

float SeaSDF(in Sea sea, vec3 p) {
    vec3 q = transpose(sea.transform.rotation) * (p - sea.transform.position);
    float flatSea = q.y;
    float noiseVal = FBM(vec3(q.xz * 1.5, iTime * 0.4));
    float waveOffset = 0.05 * (noiseVal - 0.5);
    return flatSea - waveOffset;
}

vec2 WaveSlope(vec2 xz) {
    float e = 0.15;
    float hx1 = WaveHeight(vec3(xz.x + e, 0.0, xz.y));
    float hx2 = WaveHeight(vec3(xz.x - e, 0.0, xz.y));
    float hz1 = WaveHeight(vec3(xz.x, 0.0, xz.y + e));
    float hz2 = WaveHeight(vec3(xz.x, 0.0, xz.y - e));
    return vec2((hx1 - hx2) / (2.0 * e), (hz1 - hz2) / (2.0 * e));
}

Barrel MakeBarrel(in Sea sea, vec2 xz, float phase) {
    float waveY = WaveHeight(vec3(xz.x, 0.0, xz.y));
    float bob   = 0.04 * sin(iTime * 1.4 + phase * 1.7);
    float baseY = sea.transform.position.y + waveY + 0.4 + bob;

    vec2  slope = WaveSlope(xz);
    float amp   = 0.9 + 0.5 * sin(phase * 3.7);
    float fRock = 0.8 + 0.35 * sin(phase * 2.3 + 1.0);
    float spinSpeed = 0.15 + 0.12 * fract(phase * 0.618 + 0.31);

    float leanX = 0.18 * sin(phase * 5.1 + 0.4);
    float leanZ = 0.15 * sin(phase * 4.3 + 2.2);

    float rockX =
        atan(slope.y)  * amp +
        (0.10 + 0.04 * sin(phase * 1.9)) * sin(iTime * fRock + phase * 2.1) +
        leanX;
    float rockZ =
        -atan(slope.x) * amp +
        (0.08 + 0.05 * sin(phase * 2.7)) * sin(iTime * (fRock * 0.83) + phase * 1.3) +
        leanZ;
    float spin = iTime * spinSpeed + phase * 1.7;

    Barrel barrel;
    barrel.transform.position = vec3(xz.x, baseY, xz.y);
    barrel.transform.rotation = RotateX(rockX) * RotateZ(rockZ) * RotateY(spin);
    barrel.color = vec4(1.0, 0.5, 0.2, 1.0);
    return barrel;
}

Scene BuildScene() {
    Scene s;
    s.sea.transform.position = vec3(0.0, -0.2, 0.0);
    s.sea.transform.rotation = mat3(1.0);
    s.sea.color = vec4(0.0, 0.25, 0.5, 1.0);

    for (int i = 0; i < 3; i++) {
        s.barrels[i] = MakeBarrel(s.sea, BARREL_POS[i], BARREL_PHASE[i]);
    }
    return s;
}

float MapDistance(in Scene scene, vec3 p, bool detailed) {
    float dMin = SeaSDF(scene.sea, p);

    for (int i = 0; i < 3; i++) {
        vec3  toC   = p - scene.barrels[i].transform.position;
        float bound = length(toC) - BOUND_RADIUS;
        if (bound < dMin) {
            float d = (bound > 0.35) ? bound : BarrelSDF(scene.barrels[i], p, detailed);
            dMin = min(dMin, d);
        }
    }
    return dMin;
}

Material Shade(in Scene scene, vec3 p, out int hitBarrel) {
    float dMin = SeaSDF(scene.sea, p);
    hitBarrel = -1;

    Material mat;
    mat.albedo    = pow(scene.sea.color.rgb, vec3(2.2));
    mat.roughness = 0.07 + 0.06 * FBM(vec3(p.xz * 3.0, iTime * 0.3));
    mat.metallic  = 0.0;
    mat.cavity    = 1.0;

    for (int i = 0; i < 3; i++) {
        vec3  toC   = p - scene.barrels[i].transform.position;
        float bound = length(toC) - BOUND_RADIUS;
        if (bound < dMin) {
            float d = (bound > 0.35) ? bound : BarrelSDF(scene.barrels[i], p, true);
            if (d < dMin) {
                dMin = d;
                hitBarrel = i;

                vec3 q  = BarrelLocal(scene.barrels[i], p);
                vec2 uv = BarrelUV(q);

                vec4 tex = texture(iChannel0, uv);
                vec4 prm = textureLod(iChannel1, uv, 0.0);

                vec3 albedo = pow(tex.rgb, vec3(2.2));
                albedo = mix(albedo, IRON_F0, 0.7 * prm.g);
                albedo = mix(albedo, RUST_COL, 0.85 * prm.b);

                mat.albedo    = albedo;
                mat.roughness = prm.r;
                mat.metallic  = prm.g;
                mat.cavity    = prm.a;
            }
        }
    }

    if (hitBarrel < 0) {
        float foam = 0.0;
        for (int i = 0; i < 3; i++) {
            vec2  b    = BARREL_POS[i];
            float ph   = BARREL_PHASE[i];
            float dist = length(p.xz - b);

            float mask = FBM(vec3(p.xz * 4.5 + ph * 7.0, iTime * 0.6));
            mask = smoothstep(0.35, 0.75, mask);

            float ring = smoothstep(0.95, 0.55, dist) * smoothstep(0.30, 0.55, dist);

            float ripple = 0.5 + 0.5 * sin(dist * 14.0 - iTime * 3.0 + ph * 2.0);
            ripple = smoothstep(0.45, 0.95, ripple) * smoothstep(2.2, 0.6, dist);

            foam += ring * (0.5 + 0.5 * mask) + ripple * 0.55 * mask;
        }
        float f = clamp(foam, 0.0, 1.0) * 0.9;
        mat.albedo    = mix(mat.albedo, pow(vec3(0.95, 0.98, 1.0), vec3(2.2)), f);
        mat.roughness = mix(mat.roughness, 0.6, f);
    }

    return mat;
}

float CalculateShadow(in Scene scene, vec3 p) {
    vec3  l   = normalize(light);
    float t   = 0.05;
    float res = 1.0;
    for (int i = 0; i < 24; i++) {
        float d = MapDistance(scene, p + l * t, false);
        res = min(res, 10.0 * d / t);
        t  += clamp(d, 0.02, 0.5);
        if (res < 0.01 || t > 25.0) break;
    }
    return clamp(res, 0.0, 1.0);
}

float CalculateAO(in Scene scene, vec3 p, vec3 n) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 4; i++) {
        float h = 0.03 + 0.10 * float(i);
        float d = MapDistance(scene, p + n * h, false);
        occ += (h - d) * sca;
        sca *= 0.8;
    }
    return clamp(1.0 - 2.5 * occ, 0.0, 1.0);
}

bool Raycast(in Scene scene, vec3 eye, vec3 dir, out vec3 hit) {
    const int   maxSteps     = 64;
    const float maxDist      = 100.0;
    const float hitThreshold = 0.01;
    const float coef = 0.9;

    float t = 0.0;
    for (int i = 0; i < maxSteps; i++) {
        vec3  p = eye + t * dir;
        float d = MapDistance(scene, p, true);
        if (d < hitThreshold) { hit = p; return true; }
        t += coef * d;
        if (t > maxDist) break;
    }
    return false;
}

float DistributionGGX(float NoH, float alpha) {
    float a2 = alpha * alpha;
    float d  = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float GeometrySchlickGGX(float NoX, float k) {
    return NoX / (NoX * (1.0 - k) + k);
}

float GeometrySmith(float NoV, float NoL, float roughness) {
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    return GeometrySchlickGGX(NoV, k) * GeometrySchlickGGX(NoL, k);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EnvBRDFApprox(vec3 F0, float roughness, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2  AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

vec3 CalculatePBR(in Material m, vec3 p, vec3 n, vec3 v, float shadow, float ao) {
    vec3  l   = normalize(light - p);
    vec3  h   = normalize(l + v);
    float NoL = max(dot(n, l), 0.0);
    float NoV = max(dot(n, v), 1e-3);
    float NoH = max(dot(n, h), 0.0);
    float HoV = max(dot(h, v), 0.0);

    float rough = clamp(m.roughness, 0.045, 1.0);
    float alpha = rough * rough;
    vec3  F0    = mix(vec3(0.04), m.albedo, m.metallic);

    float D = DistributionGGX(NoH, alpha);
    float G = GeometrySmith(NoV, NoL, rough);
    vec3  F = FresnelSchlick(HoV, F0);

    vec3 specular = D * G * F / max(4.0 * NoV * NoL, 1e-3);
    vec3 kD       = (1.0 - F) * (1.0 - m.metallic);
    vec3 diffuse  = kD * m.albedo / PI;

    vec3 direct = (diffuse + specular) * SUN_COLOR * NoL * shadow;

    vec3 R          = reflect(-v, n);
    vec3 Fenv       = EnvBRDFApprox(F0, rough, NoV);
    vec3 irradiance = SkyGradient(n);
    vec3 prefiltered = mix(SkyGradient(R), irradiance, rough * rough);

    vec3 kDenv   = (1.0 - Fenv) * (1.0 - m.metallic);
    vec3 ambient = (kDenv * m.albedo * irradiance + prefiltered * Fenv) * ao * m.cavity;

    return direct + ambient;
}

vec3 Normal(in Scene scene, vec3 z, float d) {
    float e = max(d * 0.5, eps);
    const vec2 k = vec2(1.0, -1.0);
    vec3 n = 
        k.xyy * MapDistance(scene, z + k.xyy * e, true) +
        k.yyx * MapDistance(scene, z + k.yyx * e, true) +
        k.yxy * MapDistance(scene, z + k.yxy * e, true) +
        k.xxx * MapDistance(scene, z + k.xxx * e, true);
    return normalize(n);
}

vec3 ApplyBarrelNormalMap(vec3 n, in Barrel barrel, vec3 q, vec2 uv) {
    float e = 0.004;
    float hL = texture(iChannel0, uv - vec2(e, 0.0)).a;
    float hR = texture(iChannel0, uv + vec2(e, 0.0)).a;
    float hD = texture(iChannel0, uv - vec2(0.0, e)).a;
    float hU = texture(iChannel0, uv + vec2(0.0, e)).a;

    float dU = (hR - hL) / (2.0 * e);
    float dV = (hU - hD) / (2.0 * e);

    float ang = atan(q.z, q.x);
    vec3 tangentLocal   = vec3(-sin(ang), 0.0, cos(ang));
    vec3 bitangentLocal = vec3(0.0, 1.0, 0.0);

    vec3 tangentWorld   = barrel.transform.rotation * tangentLocal;
    vec3 bitangentWorld = barrel.transform.rotation * bitangentLocal;

    const float strength = 0.35;
    vec3 bumped = n - strength * dU * tangentWorld - strength * dV * bitangentWorld;
    return normalize(bumped);
}

vec3 ACESFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    vec2 m = iMouse.xy / iResolution.xy;
    if (iMouse.x == 0.0 && iMouse.y == 0.0) m = vec2(0.5, 0.45);

    float yaw    = -(m.x - 0.5) * 6.28318;
    float pitch  = clamp((m.y - 0.5) * 2.5 + 0.35, 0.05, 1.2);
    float radius = 6.0;

    vec3 camPos = vec3(
        radius * cos(pitch) * sin(yaw),
        radius * sin(pitch),
        radius * cos(pitch) * cos(yaw)
    );
    vec3 target = vec3(0.0, 0.0, 0.0);
    vec3 fwd    = normalize(target - camPos);
    vec3 right  = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
    vec3 up     = cross(right, fwd);

    vec3 dir = normalize(fwd + uv.x * right + uv.y * up);

    Scene scene = BuildScene();

    vec3 hit = vec3(0.0);
    vec3 col = vec3(0.0);

    if (Raycast(scene, camPos, dir, hit)) {
        int hitBarrel = -1;
        Material mat = Shade(scene, hit, hitBarrel);
        vec3 n = Normal(scene, hit, 1e-3);

        if (hitBarrel >= 0) {
            vec3 q   = BarrelLocal(scene.barrels[hitBarrel], hit);
            vec2 buv = BarrelUV(q);
            n = ApplyBarrelNormalMap(n, scene.barrels[hitBarrel], q, buv);
        }

        vec3  v  = normalize(camPos - hit);
        float sh = CalculateShadow(scene, hit + n * 0.02);
        float ao = CalculateAO(scene, hit, n);

        col = CalculatePBR(mat, hit, n, v, sh, ao);
    } else {
        col = SkyColor(dir);
    }

    col = ACESFilm(col * EXPOSURE);
    col = pow(col, vec3(1.0 / 2.2));

    fragColor = vec4(col, 1.0);
}

void main() {
    // Shadertoy's screen origin is at the bottom left; Vulkan's is at the top left.
    mainImage(outColor, vec2(gl_FragCoord.x, iResolution.y - gl_FragCoord.y));
}
