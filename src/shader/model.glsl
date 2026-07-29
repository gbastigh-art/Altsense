@module model

@include ctypes.glsl
@include common.glsl

@vs vs
@include_block common

in vec3 a_position;
in vec3 a_normal;
in vec2 a_texcoord0;

in float a_id;
in float a_buffer_index;
in float a_model_flags;
in float a_model_type_flags;
in vec4 a_t0;
in vec4 a_t1;
in vec4 a_t2;
in vec4 a_t3;

uniform vs_params {
    mat4 view;
    mat4 proj;
};

out vec3 pos_w;
out vec3 pos_v;
out vec3 pos_l;
out vec3 n_w;
out vec3 n_v;
out vec2 uv;

flat out int id;
flat out int buffer_index;
flat out int flags;
flat out int type_flags;

void main() {
    const mat4 model = mat4(a_t0, a_t1, a_t2, a_t3);

    id = floatBitsToInt(a_id);
    buffer_index = floatBitsToInt(a_buffer_index);
    flags =  floatBitsToInt(a_model_flags);
    type_flags = floatBitsToInt(a_model_type_flags);

    pos_l = a_position * vec3(length(model[0].xyz), length(model[1].xyz), length(model[2].xyz));
    pos_w = (model * vec4(a_position, 1.0)).xyz;
    pos_v = vec3(view * vec4(pos_w, 1.0)).xyz;
    pos_v = round(pos_v * 64.0) / 64.0;
    pos_w = (inverse(view) * vec4(pos_v, 1.0)).xyz;

    gl_Position = proj * vec4(pos_v, 1.0);

    if ((flags & MRF_FIRST_PERSON) != 0) {
        gl_Position.z = 0.0 + (gl_Position.z / 1000.0);
    }

    uv = a_texcoord0.xy;
 
    mat3 normal_mtx = transpose(inverse(mat3(model)));
    n_w = normal_mtx * a_normal;
    n_v = (view * vec4(n_w, 0.0)).xyz;
}
@end

@fs fs_normal

uniform sampler smp_nearest;

@include_block common
@include_block tex_atlas
@include_block level_buffers

uniform fs_params_normal {
    int tick;
    float time_s;
};

in vec3 pos_w;
in vec3 pos_v;
in vec3 pos_l;
in vec3 n_w;
in vec3 n_v;
in vec2 uv;

flat in int id;
flat in int buffer_index;
flat in int flags;
flat in int type_flags;

layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_pos_w_id;
layout(location=2) out vec4 frag_pos_v_index_type_flags;
layout(location=3) out vec4 frag_normal_uv;

void main() {
    model_render_data_t rd_model = model_render_data[buffer_index];

    tex_atlas_entry entry;
    tex_atlas_lookup(rd_model.tex, tick, entry);

    vec4 color =
        tex_atlas_sample(vec3(entry.mi + (entry.size * uv), entry.layer));

    if (color.a < 0.0001) { discard; }

    if ((flags & MRF_BULLET) != 0) {
        const vec3 pos_q = (round(pos_l * 64) / 64) + vec3(tick / 3);
        if (rand(pos_q) > pos_l.x + 0.5) {
            discard;
        }

        if ((pos_l.x + 0.5) - (1.0 - saturate((time_s - rd_model.spawn_time) / 0.05)) < 0) {
            discard;
        }
    }

    if (rd_model.corpse_tick != 0) {
        const float since_death = 
            (tick - rd_model.corpse_tick) * (1.0 / TICKS_PER_SECOND);

        color = vec4(1);

        const vec3 offs = vec3((id % 10) + rd_model.corpse_tick);
        const vec3 scale = vec3(0.8);

        const float s_center = simplex_3d(offs + (scale * rd_model.m_centroid));
        const float s_l = simplex_3d(offs + (scale * pos_l));
        const float s_diff = abs(s_l - s_center);

        // goes from 0 -> 2 as time since death increases
        const float threshold =
            2.0 * ease_in_expo(since_death / ENTITY_CORPSE_TIME_S);

        if (s_diff < threshold) {
            discard;
        }

        color.rgb =
            mix(vec3(0.0), vec3(1.0), saturate((s_diff - threshold) / 0.5));
    }

    const int index_type_flags = (buffer_index << 16) | type_flags;

    color.rgb = mix(color.rgb, rd_model.tint.rgb, rd_model.tint.a);


    frag_color = vec4(color.rgb, 1);
    frag_pos_w_id = vec4(pos_w, intBitsToFloat(id));
    frag_pos_v_index_type_flags = vec4(pos_v, intBitsToFloat(index_type_flags));
    frag_normal_uv = vec4(normal_encode(n_v), uv);
}
@end

@fs fs_phantom

in vec3 pos_w;
in vec3 pos_v;
in vec3 pos_l;
in vec3 n_w;
in vec3 n_v;
in vec2 uv;

flat in int id;
flat in int buffer_index;
flat in int flags;
flat in int type_flags;

layout(location=0) out vec4 frag_color;

void main() {
    frag_color = vec4(1);
}
@end

@program program_normal vs fs_normal
@program program_phantom vs fs_phantom
