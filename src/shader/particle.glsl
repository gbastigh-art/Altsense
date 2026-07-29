@module particle

@include ctypes.glsl
@include common.glsl

@vs vs
@include_block common

in vec3 a_pos;
in vec2 a_uv;

// per-instance (see particle_inst_t)
in vec3  i_pos;
in vec2  i_size;
in vec3  i_vel;
in vec3  i_dir;
in float i_id;
in float i_type;
in float i_tex_id;
in float i_flags;
in float i_start;
in float i_color;
in float i_hsva;

uniform vs_params {
    float yaw;
    float pitch;
    vec3 cam_right;
    vec3 cam_dir;
    vec3 cam_up;
    vec3 cam_pos;
    mat4 view;
    mat4 proj;
    mat4 view_no_pitch;
    int tick;
    float time_s;
};

out vec3 pos_l;
out vec2 pos_s;
out vec3 pos_w;
out vec3 pos_v;
out vec2 uv;
out vec2 size;
out vec3 vel;
out vec3 dir;

flat out int id;
flat out int type;
flat out int tex_id;
flat out int flags;
flat out int start;
flat out vec4 color;
flat out vec4 hsva;

void main() {
    id = floatBitsToInt(i_id);
    type = floatBitsToInt(i_type);
    tex_id = floatBitsToInt(i_tex_id);
    flags = floatBitsToInt(i_flags);
    start = floatBitsToInt(i_start);
    color = float_to_vec4(i_color);
    hsva = float_to_vec4(i_hsva);
    vel = i_vel;
    dir = i_dir;

    pos_l = a_pos;

    size = i_size;
    bool face = false;
    bool no_transform = false;

    if (type == PARTICLE_TYPE_RICOCHET) {
        size = vec2(0.25);
        size *= vec2(0.1, clamp(length(i_vel) * 0.25, 1.0 / PX_PER_UNIT, 2.0));
    } else if (type == PARTICLE_TYPE_BLOOD) {
        float mag = clamp(length(i_vel) * 0.25, 1.0 / PX_PER_UNIT, 2.0);
        size = 0.8 * vec2(1.0, min(mag * 4, 1.6));
        face = false;
        no_transform = true;
    }  else if (type == PARTICLE_TYPE_SAC_GAS) {
        size = vec2(0.25f);
        face = true;
    }

    // TODO(particles)
    vec3 pos_t;

    pos_s = (a_pos.xz - 0.5) * size.xy;

    if (no_transform) {
        pos_t = vec3(pos_s.x, 0.0f, pos_s.y);
    } if (face) {
        pos_t =
            (cam_right * (a_pos.x - 0.5) * size.x)
                + (cam_up * (a_pos.z - 0.5) * size.y);
    } else {
        pos_s = (a_pos.xz - 0.5) * size.xy;
        pos_t =
            vec3(
                size.x * ((a_pos.x - 0.5f) * cos(yaw - PI_2)),
                size.x * ((a_pos.x - 0.5f) * sin(yaw - PI_2)),
                size.y * (a_pos.z - 0.5f));
    }

    mat4 model = mat4_identity();
    model *= mat4_translate_make(i_pos);

    if (type == PARTICLE_TYPE_RICOCHET) {
        model *= mat4_rotate_make_from_to(vec3(0, 0, 1), normalize(i_vel));
    } else if (type == PARTICLE_TYPE_BLOOD) {
        const vec3 dir_np = -view_no_pitch[2].xyz;
        // const vec3 up_np = view_no_pitch[0].xyz;
        // const vec3 right_np = view_no_pitch[1].xyz;

        model *= mat4_rotate_make_from_to(vec3(0, 0, 1), normalize(i_vel));
        model *= mat4_rotate_make_from_to(vec3(0, -1, 0), dir_np);
        model *= mat4_translate_make(vec3(0, 0, +(size.y / 2.0)));
    }

    pos_w = (model * vec4(pos_t, 1.0)).xyz;
    pos_v = (view * vec4(pos_w, 1.0)).xyz;
    gl_Position = proj * vec4(pos_v, 1.0);
    uv = a_uv;
}
@end

@fs fs

uniform sampler smp_nearest;

@include_block common
@include_block tex_atlas

uniform fs_params {
    int tick;
    float time_s;
    vec3 cam_right;
    vec3 cam_dir;
    vec3 cam_up;
    mat4 view;
};

in vec3 pos_l;
in vec2 pos_s;
in vec3 pos_w;
in vec3 pos_v;
in vec2 uv;
in vec2 size;
in vec3 vel;
in vec3 dir;

flat in int id;
flat in int type;
flat in int tex_id;
flat in int flags;
flat in int start;
flat in vec4 color;
flat in vec4 hsva;

layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_pos_w_id;
layout(location=2) out vec4 frag_pos_v_index_type_flags;
layout(location=3) out vec4 frag_normal_uv;

void main() {
    // compute pixel position from pos_s (world units) -> pixel units
    const ivec2 pos_s_px = ivec2(floor(pos_s * PX_PER_UNIT));
    const ivec2 pos_l_px = ivec2(floor(pos_l.xz * PX_PER_UNIT));

    vec3 color_ex = vec3(0);

    if (type == PARTICLE_TYPE_RICOCHET) {
        const float offs = id % 17;

        const vec2 dist = abs(vec2(pos_s_px) / PX_PER_UNIT);
        const float r = rand(vec2(pos_s_px + (id % 17)));
        if (r < (dist.y - 0.1) * 3.0) {
            discard;
        }
    } else if (type == PARTICLE_TYPE_BLOOD) {
        // pixellated distance from center
        const vec2 from_center = abs(vec2(pos_s_px) / PX_PER_UNIT) / size;
        float dist = length(from_center) / (1.0 / sqrt(2));

        float elapsed = (tick - start) / 15.0;
        dist += elapsed;

        vec3 seed = vec3(vec2(pos_s_px) / PX_PER_UNIT, id % 33);
        seed.z += elapsed / 8.0;
        const float ref = fbm(vec3(0, 0, seed.z));
        seed.y *= 0.9;
        seed.x *= 2.0;

        if (abs(ref - fbm(seed)) > 0.025 - (dist * dist * 0.01)) {
            discard;
        }

        // brighter centers, darker edges
        color_ex.r += (0.25 - dist) * 0.3;
    } else if (type == PARTICLE_TYPE_SAC_GAS) {
        // pixellated distance from center
        const vec2 from_center = abs(vec2(pos_s_px) / PX_PER_UNIT) / size;
        float dist = length(from_center) / (1.0 / sqrt(2));

        float elapsed = (tick - start) / 60.0;
        dist += elapsed;

        vec3 seed = vec3(vec2(pos_s_px) / PX_PER_UNIT, id % 33);
        seed.z += elapsed / 3.0;
        seed.x *= 0.9;
        seed.y *= 1.2;
        const float ref = fbm(vec3(0, 0, seed.z));

        if (abs(ref - fbm(seed)) > 0.02 - (dist * dist * 0.01)) {
            discard;
        }

        color_ex.r += (0.125 - dist) * 0.3;
    }

    tex_atlas_entry entry;
    tex_atlas_lookup(tex_id, tick, entry);

    const vec2 uv1 = entry.mi + (mod(uv, entry.size) * entry.size);
    frag_color = tex_atlas_sample(vec3(uv1, entry.layer));
    if (frag_color.a < 0.0001) { discard; }

    frag_color *= color;
    frag_color.rgb = offset_with_hsva(frag_color, hsva).rgb;

    frag_color = vec4(frag_color.rgb + color_ex, 1);
    frag_pos_w_id = vec4(pos_w, intBitsToFloat(type));
    frag_pos_v_index_type_flags = vec4(pos_v, intBitsToFloat(RENDER_FLAG_PARTICLE));
    frag_normal_uv = vec4(normal_encode(vec3(0, 0, 1)), uv);
}
@end

@program program vs fs
