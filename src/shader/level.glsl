@module level

@include ctypes.glsl
@include common.glsl

@vs vs
@include_block common

in vec3 a_position;
in vec3 a_n_w;
in vec2 a_texcoord0;
in float a_buffer_index;
in float a_type_flags;

uniform vs_params {
    mat4 view;
    mat4 proj;
};

out vec3 pos_w;
flat out vec3 n_w;
out vec3 pos_v;
out vec2 uv;

flat out int buffer_index;
flat out int type_flags;

void main() {
    buffer_index = floatBitsToInt(a_buffer_index);
    type_flags = floatBitsToInt(a_type_flags);
    pos_w = a_position;
    n_w = a_n_w;

    if ((type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_DECAL) {
        pos_w += n_w * (0.025 + (((buffer_index % 100) / 100.0) * 0.025));
    }

    pos_v = vec3(view * vec4(pos_w, 1.0)).xyz;

    //float curve = saturate(length(pos_v.xz) / 24.0);
    //curve = pow(2, 10 * curve - 10);
    //curve = 2 * pow(curve, 2);
    //vec3 pos_w_curve = pos_w;
    //pos_w_curve.z -= curve;
    //vec3 pos_v_curve = vec3(view * vec4(pos_w_curve, 1.0)).xyz;
    //gl_Position = proj * vec4(pos_v_curve, 1.0);
    gl_Position = proj * vec4(pos_v, 1.0);

    uv = a_texcoord0;
}
@end

@fs fs

uniform sampler smp_nearest;

@include_block common
@include_block tex_atlas
@include_block level_buffers

uniform fs_params {
    int is_portal_pass;
    int visopt;
    int tick;
    mat4 view_base;

    float fog_dist;
    int fade_enabled;
    vec2 fade_origin;
};

in vec3 pos_w;
in vec3 n_w;
in vec3 pos_v;
in vec2 uv;
flat in int buffer_index;
flat in int type_flags;

layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_pos_w_id;
layout(location=2) out vec4 frag_pos_v_index_type_flags;
layout(location=3) out vec4 frag_normal_uv;

bool should_fade(vec3 pos_w) {
    const float buffer_zone = 8.0;

    const float dist = distance(fade_origin, pos_w.xy);
    if (dist < fog_dist - (buffer_zone / 2.0)) {
        return false;
    }

    float factor =
        saturate((dist - (fog_dist - (buffer_zone / 2.0))) / buffer_zone);

    const int DITHER_MATRIX[64] = {
         0, 32, 8,  40, 2,  34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44, 4,  36, 14, 46, 6,  38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43, 1,  33, 9,  41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47, 7,  39, 13, 45, 5,  37,
        63, 31, 55, 23, 61, 29, 53, 21
    };

    const ivec3 pos_px = ivec3(pos_w * PX_PER_UNIT);
    int i_dither = ((pos_px.y % 8) * 8) + (pos_px.x % 8);
    i_dither += (pos_px.z * 17);
    i_dither %= 64;
    i_dither += (tick / 10) % 3;
    factor += 0.25 * ((DITHER_MATRIX[i_dither] / 64.0) - 0.5);

    return factor > 0.5;
}

void main() {
    gl_FragDepth = gl_FragCoord.z;

    if (is_portal_pass == 1) {
        frag_color = vec4(0);
        frag_pos_w_id = vec4(pos_w, 0);
        frag_pos_v_index_type_flags = vec4(0);
        frag_normal_uv = vec4(0);
        return;
    }

    int id = -1;
    const int render_type = type_flags & RENDER_TYPE_MASK;

    frag_color = vec4(0);

    if (render_type == RENDER_TYPE_SECTOR) {
        id = (_LT_SECTOR << 16) | buffer_index;
    } else if (render_type == RENDER_TYPE_SIDE) {
        id = (_LT_SIDE << 16) | buffer_index;
    } else if (render_type == RENDER_TYPE_DECAL) {
        id = (_LT_DECAL << 16) | buffer_index;

        const decal_render_data_t rd_decal = decal_render_data[buffer_index];

        // discard if transparent
        tex_atlas_entry entry;
        tex_atlas_lookup(int(rd_decal.tex), tick, entry);

        // quantize uvs to world-space pixels PRE-ROTATION to ensure that all
        // near UVs map to the same rotated UV (if rotated)
        vec2 pos_surf, uv_s, uv_q;

        pos_surf = rd_decal.pos + ((uv - vec2(0.5)) * rd_decal.size);
        pos_surf = floor(pos_surf * PX_PER_UNIT) / PX_PER_UNIT;
        uv_q = ((pos_surf - rd_decal.pos) / rd_decal.size) + vec2(0.5);

        uv_s = uv_rotate(uv_q, rd_decal.rotation);
        uv_s = clamp(uv_s, vec2(0.0001), vec2(0.9999));

        const vec2 uv_atlas = entry.mi + (uv_s * entry.size);
        const vec4 color = tex_atlas_sample(vec3(uv_atlas, entry.layer));

        if (color.a < 0.001) {
            discard;
        }

        frag_color.rgb = color.rgb;
        frag_color.a = 1.0;
        frag_color.rgb = mix(frag_color.rgb, rd_decal.tint.rgb, rd_decal.tint.a);

        gl_FragDepth -= 0.0000001 * (((buffer_index + 50) % 100) / 100.0);
    } 

    // need to use view_base for n_v so that portals don't mess things up
    const vec3 n_v = (view_base * vec4(n_w, 0.0)).xyz;

    int index_type_flags = (buffer_index << 16) | type_flags;

    if (fade_enabled != 0 && should_fade(pos_w)) {
        index_type_flags |= RENDER_FLAG_SKY;
    }

    frag_pos_w_id = vec4(pos_w, intBitsToFloat(id));
    frag_pos_v_index_type_flags = vec4(pos_v, intBitsToFloat(index_type_flags));
    frag_normal_uv = vec4(normal_encode(n_v), uv);
}
@end

@program program vs fs
