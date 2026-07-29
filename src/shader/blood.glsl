@module blood

@include ctypes.glsl
@include common.glsl

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

@include_block common

uniform fs_params {
    int id;
    ivec2 size_px;
};

in vec2 uv;
out vec4 frag_color;

void main() {
    // compute pixel position
    const ivec2 pos_px = ivec2(floor(uv  * size_px));
    const vec2 pos_q = vec2(pos_px) / PX_PER_UNIT;
    const vec2 from_center = abs(uv - vec2(0.5));
    const float dist =
        length(vec2(from_center.x * 0.9, from_center.y * 1.1)) / (1.0 / sqrt(2));

    vec3 seed = vec3(pos_q, id % 33);
    seed.x *= 1.5 * 0.9;
    seed.y *= 1.5 * 4.0;

    const float ref = fbm(vec3(vec2(0.5) * size_px, seed.z));

    if (abs(ref - fbm(seed)) > 0.11 - (dist * dist * 0.24)) {
        frag_color = vec4(0);
    } else {
        const float s = simplex_3d(seed);
        frag_color.rgb = vec3(0.8, 0.2, 0.2);
        frag_color.r -= dist * 0.4;
        frag_color.r += s * 0.25;
        frag_color.a = 1.0;
    }
}
@end

@program program vs fs
