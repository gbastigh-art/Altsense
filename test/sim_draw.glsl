@module sim_draw

@include ../src/shader/ctypes.glsl

@vs vs
in vec2 position;
in vec2 texcoord0;

uniform vs_params {
	mat4 model;
    mat4 view;
    mat4 proj;
};

out vec2 uv;
void main() {
    gl_Position = proj * view * model * vec4(position, 0.0, 1.0);
    uv = texcoord0;
}
@end

@fs fs

float saturate(float x) { return clamp(x, 0, 1); }
vec2 saturate(vec2 v) { return vec2(saturate(v.x), saturate(v.y)); }
vec3 saturate(vec3 v) { return vec3(saturate(v.x), saturate(v.y), saturate(v.z)); }

vec3 rgb_to_hsv(vec3 rgb) {
    float r = rgb.r, g = rgb.g, b = rgb.b;

    float K = 0.f;
    if (g < b)
    {
        // swap(g, b);
        float t = g;
        g = b;
        b = t;
        K = -1.f;
    }
    if (r < g)
    {
        // swap(r, g);
        float t = r;
        r = g;
        g = t;
        K = -2.f / 6.f - K;
    }

    const float chroma = r - (g < b ? g : b);

    vec3 hsv;
    hsv.x = abs(K + (g - b) / (6.f * chroma + 1e-20f));
    hsv.y = chroma / (r + 1e-20f);
    hsv.z = r;
    return hsv;
}

// Convert hsv floats ([0-1],[0-1],[0-1]) to rgb floats ([0-1],[0-1],[0-1]), from Foley & van Dam p593
// also http://en.wikipedia.org/wiki/HSL_and_HSV
vec3 hsv_to_rgb(vec3 hsv) {
    float h = hsv.x, s = hsv.y, v = hsv.z;
    if (s == 0.0f) {
        // gray
        return vec3(v);
    }

    h = mod(h, 1.0f) / (60.0f / 360.0f);
    int   i = int(h);
    float f = h - float(i);
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    float r, g, b;
    if (i == 0) { r = v; g = t; b = p; }
    else if (i == 1) { r = q; g = v; b = p; }
    else if (i == 2) { r = p; g = v; b = t; }
    else if (i == 3) { r = p; g = q; b = v; }
    else if (i == 4) { r = t; g = p; b = v; }
    else { r = v; g = p; b = q; }
    return vec3(r, g, b);
}

struct gpu_particle { vec2 pos; vec2 vel; float density; };

readonly buffer particle_buffer {
    gpu_particle particles[];
};

uniform fs_params {
    int n_particles;
    float kern_radius;
    vec2 bounds;
    float disp_radius;
    float disp_scale;
    vec3 density_low_color;
    vec3 density_high_color;
};

in vec2 uv;
out vec4 frag_color;

#define PI 3.14159265359

float k_poly6(float d, float r) {
    if (d > r) { return 0.0; }
    const float v = ((r * r) - (d * d));
    const float poly6 = (4.0 / (PI * pow(r, 8.0)));
    return v * v * v * poly6;
}

void main() {
    const vec2 pos = vec2(uv.x, 1.0 - uv.y) * bounds;

    vec2 vel = vec2(0);
    float density = 0.0;
    for (int i = 0; i < n_particles; i++) {
        const float dist = length(particles[i].pos - pos);
        if (dist < disp_radius) {
            density += particles[i].density * k_poly6(dist, disp_radius);
            vel += particles[i].vel * k_poly6(dist, disp_radius);
        }
    }

    const float t = saturate(density / disp_scale);
    frag_color =
        vec4(
            hsv_to_rgb(
                mix(
                    rgb_to_hsv(density_low_color),
                    rgb_to_hsv(density_high_color),
                    t)),
            1.0);
}
@end

@program program vs fs
