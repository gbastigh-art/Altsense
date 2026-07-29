@module sprite

@include ctypes.glsl
@include common.glsl

@vs vs
@include_block common

in vec3 a_position;
in vec2 a_texcoord0;

// per-instance (see sprite_instance_internal_t)
in float a_color;
in float a_hsva;
in vec3 a_pos;
in vec2 a_size;
in float a_id;
in float a_buffer_index;
in float a_type_flags;
in float a_flags;
in float a_tex_id;
in float a_rotation;

uniform vs_params {
    float yaw;
    float pitch;
    vec3 cam_right;
    vec3 cam_up;
    mat4 view;
    mat4 proj;
};

out vec3 pos_w;
out vec3 pos_v;
out vec2 uv;

flat out vec4 color;
flat out vec4 hsva;
flat out vec2 size;
flat out int id;
flat out int buffer_index;
flat out int type_flags;
flat out int flags;
flat out int tex_id;
flat out float rotation;

void main() {
    color = float_to_vec4(a_color);
    hsva = float_to_vec4(a_hsva);
    size = a_size;
    id = floatBitsToInt(a_id);
    buffer_index = floatBitsToInt(a_buffer_index);
    type_flags = floatBitsToInt(a_type_flags);
    flags = floatBitsToInt(a_flags);
    tex_id = floatBitsToInt(a_tex_id);
    rotation = a_rotation;

    const float a_yaw = yaw - PI_2;

    vec2 texcoord = a_texcoord0;

    mat4 m_view = view;

    if ((flags & SPRITE_FLAG_FACE_CAMERA) != 0) {
        pos_w =
            a_pos
                + (cam_right * a_position.x * a_size.x)
                + (cam_up * a_position.z * a_size.y);
    } else {
        pos_w =
            vec3(
                a_pos.x + a_size.x * ((a_position.x - 0.5f) * cos(a_yaw)),
                a_pos.y + a_size.x * ((a_position.x - 0.5f) * sin(a_yaw)),
                a_pos.z + (a_size.y * a_position.z));
    }

    pos_v = (m_view * vec4(pos_w, 1.0)).xyz;
    gl_Position = proj * vec4(pos_v, 1.0);
    uv = texcoord;
}
@end

@fs fs

uniform sampler smp_nearest;

@include_block common
@include_block tex_atlas

uniform fs_params {
    int tick;
};

in vec3 pos_w;
in vec3 pos_v;
in vec2 uv;

flat in vec4 color;
flat in vec4 hsva;
flat in vec2 size;
flat in int id;
flat in int buffer_index;
flat in int type_flags;
flat in int flags;
flat in int tex_id;
flat in float rotation;

layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_pos_w_id;
layout(location=2) out vec4 frag_pos_v_index_type_flags;
layout(location=3) out vec4 frag_normal_uv;

void main() {
    tex_atlas_entry entry;
    tex_atlas_lookup(tex_id, tick, entry);

    const vec2 uv1 = entry.mi + (uv_rotate(uv, rotation) * entry.size);
    const vec4 tex_color = tex_atlas_sample(vec3(uv1, entry.layer));
    if (tex_color.a < 0.0001) { discard; }

    const vec4 final_color = offset_with_hsva(color * tex_color, hsva);

    const int index_type_flags = (buffer_index << 16) | type_flags;

    frag_color = vec4(final_color.rgb, 0);
    frag_pos_w_id = vec4(pos_w, intBitsToFloat(id));
    frag_pos_v_index_type_flags = vec4(pos_v, intBitsToFloat(index_type_flags));

    // NOTE: sprites do not have normals as they are not needed for lighting -
    // instead, we pack the sprite size
    frag_normal_uv = vec4(size, uv);
}
@end

@program program vs fs
