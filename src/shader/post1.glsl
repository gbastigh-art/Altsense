@module post1

@include ctypes.glsl
@include common.glsl

@vs vs
@include_block common

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

uniform sampler smp_nearest;
uniform sampler smp_linear;

@include_block common
@include_block tex_atlas
@include_block level_buffers

uniform texture2D d_pos_v_index_type_flags;
uniform texture2D d_normal_uv;
uniform texture2D c_color;
uniform texture2D c_light;
uniform texture2D c_extra_id_pos;
uniform texture2D bloom_image;

uniform texture3D palette_generic;
uniform texture3D palette_level;

uniform fs_params {
    mat4 view, inv_view;
    vec4 near_plane_no_pitch;
    vec3 cam_pos;
    int tick;
    float time_s;
    int visopt;
    float gamma;
    int fog_enabled;
    int no_dither256;
    int no_dither8;
    int no_bloom;
};

in vec2 uv;

layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_blur;

void main() { 
    const vec4
        s_pos_v_index_type_flags =
            texture(sampler2D(d_pos_v_index_type_flags, smp_nearest), uv),
        s_extra_id_pos =
            texture(sampler2D(c_extra_id_pos, smp_nearest), uv),
        s_normal_uv =
            texture(sampler2D(d_normal_uv, smp_nearest), uv);

    const vec3 n_v = normal_decode(s_normal_uv.xy);
    const vec3 n_w = (inv_view * vec4(n_v, 0.0)).xyz;
    const vec3 pos_w_fake =
        (inv_view * vec4(s_pos_v_index_type_flags.xyz, 1.0)).xyz;

    const int index_type_flags = floatBitsToInt(s_pos_v_index_type_flags.w);
    const int buffer_index = index_type_flags >> 16;
    const int type_flags = index_type_flags & 0xFFFF;

    frag_color = vec4(texture(sampler2D(c_color, smp_nearest), uv).rgb, 1.0);
    const ivec2 size = textureSize(sampler2D(c_color, smp_nearest), 0);

    vec2 pos_dither = s_extra_id_pos.zw;

    const int render_type = type_flags & RENDER_TYPE_MASK;

    if ((visopt & _VISOPT_NODANCE) == 0) {
        if (render_type == RENDER_TYPE_SIDE
            || (render_type == RENDER_TYPE_DECAL
                && floatBitsToInt(s_extra_id_pos.y) == 2)) {
            pos_dither.y -= (tick / 10) * (1.0 / PX_PER_UNIT);
        } else {
            pos_dither +=
                vec2(
                    ((tick + 0) / 10) % 3,
                    ((tick + 20) / 10) % 3) * (1.0 / PX_PER_UNIT);
        }
    }

    const ivec2 pos_dither_i = ivec2(floor(pos_dither.xy * PX_PER_UNIT));

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

    const float z = -s_pos_v_index_type_flags.z;

    const float blue_noise = rand(vec2(pos_dither_i));

    const int q = (((pos_dither_i.x * 17) % 8) * 8) + ((pos_dither_i.y * 31) % 8);
    const float dither_offset =
        (1.0 / 64.0)
            * mix(
                (DITHER_MATRIX[q] / 64.0) - 0.5,
                0.25 * blue_noise,
                saturate(z / 8.0));

    vec3 light_rgb = texture(sampler2D(c_light, smp_nearest), uv).rgb;
    light_rgb = max(light_rgb, vec3(0.15));
    light_rgb = pow(light_rgb, vec3(1.0 / gamma));

    vec4 bloom = texture(sampler2D(bloom_image, smp_linear), uv);

    vec3 bloom_hsv = rgb_to_hsv(bloom.rgb);
    bloom_hsv.z *= bloom.a;
    bloom.rgb = hsv_to_rgb(bloom_hsv);

    // apply some bloom pre-palette
    if (no_bloom == 0) {
        frag_color.rgb += 0.5 * bloom.rgb;
    }

    if ((visopt & _VISOPT_FULLBRIGHT) == 0) {
        frag_color.rgb =
            (frag_color.rgb * 0.15)
                + (0.80 * frag_color.rgb * light_rgb)
                + (0.05 * light_rgb);

        if ((type_flags & RENDER_FLAG_SKY) == 0) {
            frag_color.rgb += dither_offset;
        }

        if ((type_flags & RENDER_FLAG_SGL) != 0) {
            // do not palettize!
        } else if (
            (type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_MODEL
            || (type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_DECAL
            || (type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_SPRITE
            || (type_flags & RENDER_FLAG_LIQUID) != 0
            || (type_flags & RENDER_FLAG_SKY) != 0
            || (type_flags & RENDER_FLAG_PARTICLE) != 0
            || (type_flags & RENDER_FLAG_TRUE_COLOR) != 0) {
            if (no_dither256 == 0) {
                // use regular 256-color palette
                frag_color.rgb =
                    texture(
                        sampler3D(palette_generic, smp_nearest),
                        frag_color.rgb).rgb;
            }
        } else {
            if (no_dither8 == 0) {
                frag_color.rgb =
                // use level palette
                    texture(
                        sampler3D(palette_level, smp_nearest),
                        frag_color.rgb).rgb;
            }
        }

        frag_color.rgb += 0.05 * light_rgb;
    }

    // apply rest of bloom
    if (no_bloom == 0) {
        frag_color.rgb += 0.5 * bloom.rgb;
    }

    frag_blur = vec4(frag_color.rgb, 1.0);

    // apply model post overlay
    if ((type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_MODEL) {
        // TODO: use s_extra_id_pos.x == MRF_* to check if we want post before
        // grabbing the entire struct out of the buffer
        model_render_data_t rd_model = model_render_data[buffer_index];  

        if ((rd_model.flags & MRF_OVERLAY_POST) != 0
            && rd_model.tex_overlay != 0) {

            tex_atlas_entry entry;
            tex_atlas_lookup(rd_model.tex_overlay, tick, entry);

            vec2 coord = uv;

#ifdef SOKOL_MSL
            coord.y = 1.0 - coord.y;
#endif // ifdef SOKOL_MSL

            // in screen pixels
            const vec2 screen_size_px =
                textureSize(sampler2D(d_normal_uv, smp_nearest), 0);
            vec2 v = coord * screen_size_px;

            // TODO: use standard scroll rates
            float scroll = tick / 6;
            scroll *= (rd_model.flags & MRF_OVERLAY_SCROLL_INV) != 0 ? -1 : 1;

            if ((rd_model.flags & MRF_OVERLAY_SCROLL_H) != 0) {
                v.x -= scroll;
            }

            if ((rd_model.flags & MRF_OVERLAY_SCROLL_V) != 0) {
                v.y += scroll;
            }

            // screen pixels -> tex atlas uv units
            v *= 1.0f / float(TEX_ATLAS_SIZE);

            vec4 overlay_color =
                tex_atlas_sample(vec3(entry.mi + mod(v, entry.size), entry.layer));
            overlay_color.a *= rd_model.overlay_alpha;

            frag_color.rgb =
                mix(frag_color.rgb, overlay_color.rgb, overlay_color.a);
        }
    }

    if (fog_enabled != 0) {
        // TODO: fog config
        const vec3 fog = vec3(0, 0, 0);

        const vec3
            p_near = plane_project(near_plane_no_pitch, pos_w_fake),
            d_near = p_near - pos_w_fake;

        const float dist_near = length(d_near);
        const float dist = length(pos_w_fake - cam_pos);

        float fog_factor;
        fog_factor = max(dist - 16.0, 0.0) / 24.0;
        fog_factor = saturate(pow(2.0, (5.0 * fog_factor) - 5.0));

        if ((visopt & _VISOPT_FULLBRIGHT) != 0) {
            fog_factor = 0.0;
        }

        // lessen fog factor for areas with lots of light
        fog_factor = saturate(fog_factor - saturate(rgb_to_hsv(light_rgb).z / 4.0));
        frag_color.rgb = mix(frag_color.rgb, fog, fog_factor);
    }

    // adjust brightness slightly for normal angle
    if (dot(frag_color.rgb, vec3(1)) > 0.005
            && render_type != RENDER_TYPE_DECAL
            && ((type_flags & RENDER_FLAG_SKY) == 0)) {
        float v_adjust = 0.0;
        if (render_type == RENDER_TYPE_SECTOR) {
            if ((type_flags & RENDER_FLAG_IS_CEIL) != 0) {
                v_adjust = 0.025;
            } else {
                v_adjust = 0.0;
            }
        } else {
            const float factor = sin(angle_wrap_tau(atan(n_w.y, n_w.x)));
            v_adjust = 0.05 * factor;
        }

        vec3 hsv = rgb_to_hsv(frag_color.rgb);
        hsv.z += v_adjust;
        hsv.z = saturate(hsv.z) - 0.00001;
        frag_color.rgb = hsv_to_rgb(hsv);
    }

    frag_color.rgb = saturate(frag_color.rgb);
}

@end

@program program vs fs
