@module fancy_particle

@include ctypes.glsl
@include common.glsl

@vs vs
@include_block common

in vec3 a_pos;
in vec3 a_normal;
in vec2 a_uv;
in float a_vertex_index;

// per-instance (see fancy_particle_inst_t)
in float i_type;
in float i_id;
in vec3 i_pos;
in vec3 i_scale;
in vec3 i_vel;

uniform vs_params {
    mat4 view;
    mat4 proj;
    int tick;
    float time_s;
};

out vec3 pos_l;
out vec3 pos_v;
out vec3 pos_w;
out vec3 n_v;
out vec3 n_w;
out vec2 uv;

flat out int type;
flat out int id;
out vec3 vel;

void main() {
    type = floatBitsToInt(i_type);
    id = floatBitsToInt(i_id);
    uv = a_uv;
    vel = i_vel;

    int vertex_index = floatBitsToInt(a_vertex_index);

    mat4 model = mat4_identity();
    model *= mat4_translate_make(i_pos);

    if (type == PARTICLE_TYPE_RICOCHET) {
        const int v_id = gl_InstanceIndex; // id + (vertex_index * 17);
        vec3 offs;
        offs.x = 1.0 * (rand(vec2(v_id * 28149371, v_id * 63935537)) - 0.5);
        offs.y = 1.0 * (rand(vec2(v_id * 63935537, v_id * 28149371)) - 0.5);
        offs.z = 0.5 * (rand(vec2(v_id * 24682513, v_id * 24682513)) - 0.5);
        model *= mat4_translate_make(offs);
    }

    if (type == PARTICLE_TYPE_RICOCHET) {
        // model *= mat4_translate_make(vec3(0, 0, +0.5));
        model *= mat4_rotate_make_from_to(vec3(0, 0, 1), normalize(vel));
        model *= mat4_translate_make(vec3(0, 0, -0.5));
        model *= mat4_scale_make(vec3(0.03, 0.03, min(length(vel) * 0.15, 1)));
    }

    // TODO: i_scale?

    vec3 g_pos = a_pos;

    pos_l = a_pos;
    pos_w = (model * vec4(g_pos, 1.0)).xyz;
    pos_v = vec3(view * vec4(pos_w, 1.0)).xyz;

    // TODO: vertex snapping on particles?
    // pos_v = round(pos_v * 64.0) / 64.0;
    // pos_w = (inverse(view) * vec4(pos_v, 1.0)).xyz;

    n_w = a_normal;
    n_v = (view * vec4(n_w, 0.0)).xyz;

    gl_Position = proj * vec4(pos_v, 1.0);
}
@end

@fs fs

uniform sampler smp_nearest;

@include_block common
@include_block tex_atlas

uniform fs_params {
    int tick;
    float time_s;
};

in vec3 pos_l;
in vec3 pos_v;
in vec3 pos_w;
in vec3 n_v;
in vec3 n_w;
in vec2 uv;

flat in int type;
flat in int id;
in vec3 vel;

layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_pos_w_id;
layout(location=2) out vec4 frag_pos_v_index_type_flags;
layout(location=3) out vec4 frag_normal_uv;

void main() {
    //if (type == PARTICLE_TYPE_RICOCHET) {
    //    // make line segment going through middle of v3(-0.5)..(v3(+0.5)) cube,
    //    // rotated to be aligned with velocity
    //    const vec3 dir = normalize(vel);
    //    const vec3 a = dir * +0.5;
    //    const vec3 b = dir * -0.5;

    //    const vec3 proj = point_project_segment(pos_l, a, b);
    //    const float dist = length(pos_l - proj);
    //    if (dist > 0.25) {
    //        discard;
    //    }
    //}

    frag_color = vec4(vec3(1), tick / 1000.0);
    frag_pos_w_id = vec4(pos_w, intBitsToFloat(0));
    frag_pos_v_index_type_flags =
        vec4(pos_v, intBitsToFloat(RENDER_FLAG_FANCY_PARTICLE));
    frag_normal_uv = vec4(normal_encode(n_v), uv);
}
@end

@program program vs fs
