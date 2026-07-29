@module composite

@include ctypes.glsl
@include common.glsl
@include light.glsl

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

uniform sampler smp_nearest;

@include_block tex_atlas
@include_block level_buffers
@include_block light

uniform texture2D d_color;
uniform texture2D d_pos_w_id;
uniform texture2D d_pos_v_index_type_flags;
uniform texture2D d_normal_uv;

uniform texture2D shadow_image;

composite_frag_data data;

in vec2 uv;

uniform fs_params {
    mat4 proj, view;
    mat4 inv_view_proj;
    vec2 viewport;
    vec3 camera_pos;
    vec4 near_plane_no_pitch;
    float depth_near, depth_far;
    int visopt;
    float extra_bloom;
    int tick;
    float time_s;
    ivec2 blocks_offset;
    ivec2 blocks_size;
    float liquid_fall_effect;

    int sky_tex_id0;
    int sky_tex_id1;
    float sky_tex_mix;
    float sky_tex_alpha;

    float fog_dist;
    int fade_enabled;
    vec2 fade_origin;
};

layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_extra_id_pos;
layout(location=2) out vec4 frag_light;
layout(location=3) out vec4 frag_bloom;

vec4 do_light(vec4 color) {
    if (data.is_sky) {
        // no light, frag is sky
        return color;
    }

    blocks_info info;
    info.offset = ivec2(blocks_offset[0], blocks_offset[1]);
    info.size = ivec2(blocks_size[0], blocks_size[1]);

    frag_bloom = vec4(0);

    vec3 light = vec3(0);

    // round positions, normals
    composite_frag_data light_frag_data = data;

    if ((data.type_flags & RENDER_TYPE_MASK) != RENDER_TYPE_MODEL) {
        light_frag_data.pos_quantized =
            floor(data.pos_quantized * 32) / 32;
    }

    light_frag_data.n_w = round(data.n_w * 32) / 32;

    // TODO: fixes a bug with slope lighting...
    light_frag_data.pos_quantized += light_frag_data.n_w * (1.5 / 32.0);

    // normal must be quantized for lighting calculations to avoid messing
    // up the dithering due to the imprecision introduced by the 3D -> 2D
    // normal compression which is used
    light =
        light_frag(
            shadow_image,
            smp_nearest,
            light_frag_data,
            tick,
            visopt);

    light += data.extra_light;

    // entry effect (light)
    if ((data.model_render_flags & MRF_FIRST_PERSON) == 0) {
        light += liquid_fall_effect * vec3(0.12f, 0.0f, 0.0f);
    } else {
        light += liquid_fall_effect * vec3(0.25);
    }

    vec3 light_hsv = rgb_to_hsv(light);
    light_hsv.z += extra_bloom;
 
    float bloom_threshold = 0.4;

    // limit environmental bloom, lighting on models
    if ((data.type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_MODEL) {
        bloom_threshold = 0.75;
        vec3 lhsv = rgb_to_hsv(light);
        lhsv.z = min(lhsv.z, 0.7);
        light = hsv_to_rgb(lhsv);
    }

    if (light_hsv.z > bloom_threshold) {
        frag_bloom = vec4(light.rgb, light_hsv.z - bloom_threshold);
    } else {
        frag_bloom = vec4(0);
    }

    frag_bloom += data.extra_bloom;

    // entry effect (bloom)
    if ((data.model_render_flags & MRF_FIRST_PERSON) == 0) {
        frag_bloom += liquid_fall_effect * vec4(1.0f, 0.5f, 0.5f, 0.1f);
    }

    // near light (note: after bloom)
    if (true) {
        float near;
        near = 1.0 - (length(camera_pos - data.pos_w_fake) / 12.0);
        near = pow(2.0, (5 * near) - 5);
        near *= 0.5;

        vec3 hsv = rgb_to_hsv(light);
        hsv.z += near;
        hsv.z = saturate(hsv.z);
        light.rgb = hsv_to_rgb(hsv);
    }

    frag_light = vec4(light.rgb, 1); 

    return color;
}

vec3 sky_color() {
    const ivec2 size = textureSize(sampler2D(d_color, smp_nearest), 0);
    const vec3 dir =
        pixel_to_camera_dir(inv_view_proj, camera_pos, uv * vec2(size), size);

    float n = saturate(fbm((((dir + 1.0) / 2.0) * 10.0) + vec3(0, 0, time_s * 3.0)));
    n = ease_in_out_quart(n);
    n = clamp(n, 0.1, 0.9);
    vec3 color_base = n * vec3(0.9, 0.3, 0.1);

    tex_atlas_entry entry0, entry1;
    tex_atlas_lookup(sky_tex_id0, tick, entry0);
    tex_atlas_lookup(sky_tex_id1, tick, entry1);

    vec2 coord = uv;
#ifdef SOKOL_MSL
    coord.y = 1.0 - coord.y;
#endif // ifdef SOKOL_MSL

    // in screen pixels
    const vec2 screen_size_px = textureSize(sampler2D(d_color, smp_nearest), 0);
    vec2 v = coord * screen_size_px;

    // TODO: use standard scroll rates
    float scroll = 2 * (tick / 6);
    v.y += scroll;

    // screen pixels -> tex atlas uv units
    v *= 1.0f / float(TEX_ATLAS_SIZE);

    vec4 sky_tex_color0, sky_tex_color1;
    sky_tex_color0 = tex_atlas_sample(vec3(entry0.mi + mod(v, entry0.size), entry0.layer));
    sky_tex_color1 = tex_atlas_sample(vec3(entry1.mi + mod(v, entry1.size), entry1.layer));

    vec4 sky_tex_color = mix(sky_tex_color0, sky_tex_color1, sky_tex_mix);

    return mix(color_base, sky_tex_color.rgb, sky_tex_alpha * sky_tex_color.a);
}

int texture_scroll_px() {
    const int ticks =
        (tick / TEXTURE_SCROLL_TICK_DIVISOR) * TEXTURE_SCROLL_TICK_DIVISOR;

    return int(floor(ticks * float(TEXTURE_SCROLL_PX_PER_TICK)));
}

float compute_scroll() {
    return texture_scroll_px() * (1.0 / PX_PER_UNIT);
}

vec4 compute_overlay(
        int tex, float alpha, vec2 offset, bool scroll_h, bool scroll_v) {
    tex_atlas_entry entry;
    tex_atlas_lookup(tex, tick, entry);

    vec2 v = offset;

    if (scroll_h) {
        v.x += texture_scroll_px() * (1.0 / PX_PER_UNIT);
    }

    if (scroll_v) {
        v.y += texture_scroll_px() * (1.0 / PX_PER_UNIT);
    }

    vec4 overlay_color =
        tex_atlas_sample(
            vec3(
                entry.mi
                    + mod((v * PX_PER_UNIT) * TEX_ATLAS_UNIT, entry.size),
                entry.layer));

    return vec4(overlay_color.rgb, overlay_color.a * alpha);
}

// sets pos_dither, pos_ex, and pos_quantized for specified side data + UV
void set_dither_ex_quantized_pos_for_side(
        in side_render_data_t rd_side,
        vec2 uv_side) {
    const vec2 side_tangent = rd_side.b - rd_side.a;
    const vec2 side_dir = normalize(side_tangent);

    // dither pos is entirely side-relative in world units
    vec2 pos_dither =
        vec2(
            uv_side.x * length(side_tangent),
            data.pos_w.z - rd_side.z_floor);

    // pixel-snapped coords
    pos_dither = floor(pos_dither * PX_PER_UNIT) / PX_PER_UNIT;

    data.pos_dither = pos_dither;
    data.pos_ex = pos_dither;

    // "pos_quantized" is the quantized PX_PER_UNIT world position, also snapped
    // to side pixels
    data.pos_quantized =
        vec3(
            rd_side.a.xy + (pos_dither.x * side_dir),
            data.pos_w.z);

    // snap to pixel
    data.pos_quantized = floor(data.pos_quantized * PX_PER_UNIT) / PX_PER_UNIT;
}

void do_side() {
    side_render_data_t rd_side = side_render_data[data.id & 0xFFFF];
    sector_render_data_t rd_sector = sector_render_data[rd_side.sector_index];

    int texture_id = rd_side.tex_mid;

    if ((rd_side.sidemat_flags & SDMF_EZPORT) != 0) {
        if ((data.type_flags & RENDER_FLAG_SEG_BOTTOM) != 0) {
            texture_id = rd_side.tex_low;
        } else if ((data.type_flags & RENDER_FLAG_SEG_TOP) != 0) {
            texture_id = rd_side.tex_high;
        } else {
            texture_id = rd_side.tex_mid;
        }
    } else {
        const float
            z_base = rd_side.z_floor,
            split_z = data.pos_w.z - z_base;

        float
            split_bottom = rd_side.split_bottom,
            split_top = rd_side.split_top;

        if ((rd_side.sidemat_flags & SDMF_BOT_ABS) != 0) {
            split_bottom -= z_base;
        }

        if ((rd_side.sidemat_flags & SDMF_TOP_ABS) != 0) {
            split_top -= z_base;
        }

        if (split_z < split_bottom) {
            texture_id = rd_side.tex_low;
        } else if (
            split_top != split_bottom
            && ((split_top < 0 && (rd_side.z_ceil - z_base) - split_z < abs(split_top))
                || (split_top > 0 && split_z > split_top))) {
            texture_id = rd_side.tex_high;
        } else {
            texture_id = rd_side.tex_mid;
        }
    }

    tex_atlas_entry entry;
    tex_atlas_lookup(texture_id, tick, entry);

    vec2 offs = vec2(data.uv.s * length(rd_side.b - rd_side.a), data.pos_w.z);

    const vec2 base_offs = offs;

    const vec2 normal =
        normalize(
            vec2(
                -(rd_side.b.y - rd_side.a.y),
                rd_side.b.x - rd_side.a.x));

    if ((rd_side.sidemat_flags & SDMF_EZX) != 0) {
        // find length along normal
        const vec2 tangent = rotate_vec2(normal, PI_2);

        // keep quantized
        float x = min(dot(rd_side.a, tangent), dot(rd_side.b, tangent));
        x = floor(x * PX_PER_UNIT) / PX_PER_UNIT;
        offs.x += x;
    }

    offs -= (rd_side.offsets / PX_PER_UNIT);

    offs +=
        vec2(compute_scroll())
            * vec2(
                (rd_side.sidemat_flags & SDMF_SCROLL_H) != 0,
                (rd_side.sidemat_flags & SDMF_SCROLL_V) != 0);

    if ((rd_side.sidemat_flags & SDMF_PEG) != 0) {
        if ((data.type_flags & RENDER_FLAG_SEG_TOP) != 0) {
            offs.y -= rd_side.nz_ceil;
        }

        if ((data.type_flags & RENDER_FLAG_SEG_BOTTOM) != 0) {
            offs.y -= rd_side.nz_floor;
        }
    }

    set_dither_ex_quantized_pos_for_side(rd_side, data.uv); 
    data.ex = intBitsToFloat(texture_id);

    if (data.is_sky) {
        frag_color.rgb = sky_color();
    } else {
        vec2 uv_atlas =
            entry.mi + mod((offs * PX_PER_UNIT) * TEX_ATLAS_UNIT, entry.size);

        frag_color = tex_atlas_sample(vec3(uv_atlas, entry.layer));
        frag_color = offset_with_hsva(frag_color, rd_side.hsva);
    }


    const int overlay_id = rd_side.tex_overlay;
    if (overlay_id != 0 && !data.is_sky) {
        float alpha;
        if ((rd_side.sidemat_flags & SDMF_OV_SCRY) != 0) {
            const vec2 projected =
                point_project_segment(camera_pos.xy, rd_side.a, rd_side.b);
            alpha =
                (1.0 - saturate(length(camera_pos.xy - projected) / SCRY_DISTANCE));
        } else {
            alpha = rd_side.overlay_alpha;
        }

        const vec4 overlay_color =
            compute_overlay(
                overlay_id,
                alpha,
                offs,
                (rd_side.sidemat_flags & SDMF_OV_SCROLL_H) != 0,
                (rd_side.sidemat_flags & SDMF_OV_SCROLL_V) != 0);
        frag_color.rgb =
            (overlay_color.a * overlay_color.rgb)
                + ((1.0 - overlay_color.a) * frag_color.rgb);
    }

    frag_color = do_light(frag_color);
}

void do_sector() {
    sector_render_data_t rd_sector = sector_render_data[data.buffer_index];

    if ((data.type_flags & RENDER_FLAG_LIQUID) != 0) {
        data.extra_bloom += rd_sector.liquid_extra_bloom;
    }

    const bool is_ceil = (data.type_flags & RENDER_FLAG_IS_CEIL) != 0;
    int texture_id;
    vec4 hsva;

    if (is_ceil) {
        texture_id = rd_sector.tex_ceil;
        hsva = rd_sector.ceil_hsva;
    } else if ((data.type_flags & RENDER_FLAG_LIQUID) != 0) {
        texture_id = rd_sector.tex_liquid;
        hsva = vec4(rd_sector.liquid_hsv, 0);
    } else {
        texture_id = rd_sector.tex_floor;
        hsva = rd_sector.floor_hsva;
    }

    tex_atlas_entry entry;
    tex_atlas_lookup(texture_id, tick, entry);

    vec2 offs = data.pos_w.xy;

    if (is_ceil) {
        offs -= rd_sector.ceil_offsets / PX_PER_UNIT;
    } else {
        offs -= rd_sector.floor_offsets / PX_PER_UNIT;
    }

    offs +=
        vec2(compute_scroll())
            * vec2((rd_sector.sectmat_flags & SCMF_SCROLL_INV) != 0 ? -1 : 1)
            * vec2(
                (rd_sector.sectmat_flags & SCMF_SCROLL_H) != 0,
                (rd_sector.sectmat_flags & SCMF_SCROLL_V) != 0);

    const vec2 uv_atlas =
        entry.mi
            + mod(
                (offs * PX_PER_UNIT) * TEX_ATLAS_UNIT,
                entry.size);

    data.pos_dither = data.pos_w.xy;
    data.pos_quantized =
        vec3(floor(data.pos_w.xy * PX_PER_UNIT) / PX_PER_UNIT, data.pos_w.z);
    data.pos_ex = data.pos_w.xy;
    data.ex = intBitsToFloat(is_ceil ? 1 : 0);

    if (data.is_sky) {
        frag_color.rgb = sky_color();
    } else {
        frag_color = tex_atlas_sample(vec3(uv_atlas, entry.layer));
        frag_color = offset_with_hsva(frag_color, hsva);
    }

    const int overlay_id =
        is_ceil ? rd_sector.tex_overlay_ceil : rd_sector.tex_overlay_floor;

    if (overlay_id != 0 && !data.is_sky) {
        const vec4 overlay_color =
            compute_overlay(
                overlay_id,
                is_ceil ?
                    rd_sector.overlay_alpha_ceil
                    : rd_sector.overlay_alpha_floor,
                offs,
                (rd_sector.sectmat_flags & SCMF_OV_SCROLL_H) != 0,
                (rd_sector.sectmat_flags & SCMF_OV_SCROLL_V) != 0);
        frag_color.rgb =
            mix(frag_color.rgb, overlay_color.rgb, overlay_color.a);
    }

    frag_color = do_light(frag_color);
}

void do_decal() {
    const decal_render_data_t rd_decal = decal_render_data[data.buffer_index];

    // computed in level.glsl
    frag_color.rgb = data.color;

    if (rd_decal.side_index != -1) {
        const side_render_data_t rd_side =
            side_render_data[rd_decal.side_index];

        const float u =
            length(data.pos_w.xy - rd_side.a) / length(rd_side.b - rd_side.a);

        set_dither_ex_quantized_pos_for_side(
            rd_side,
            vec2(u, data.pos_w.z - rd_side.z_floor));
    } else {
        data.pos_dither = data.pos_w.xy;
        data.pos_quantized =
            vec3(floor(data.pos_w.xy * PX_PER_UNIT) / PX_PER_UNIT, data.pos_w.z);
        data.pos_ex = data.pos_w.xy;
        data.ex =
            intBitsToFloat(rd_decal.plane_type == _PLANE_TYPE_CEIL ? 1 : 0);
    }

    frag_color = do_light(frag_color);
}

void do_sprite() {
    if ((data.type_flags & RENDER_FLAG_PARTICLE) == 0) {
        const sprite_render_data_t rd_sprite =
            sprite_render_data[data.buffer_index];
        data.extra_light = rd_sprite.extra_light;
        data.extra_bloom = rd_sprite.extra_bloom;
    }

    data.n_w = vec3(0);
    data.pos_dither = vec2(0);
    data.pos_ex = vec2(0);
    data.ex = 0.0;

    frag_color = do_light(vec4(data.color, 1.0));
}

void do_particle() {
    if (data.id == PARTICLE_TYPE_RICOCHET) {
        data.extra_bloom = vec4(1.0, 0.7, 0.2, 0.5);
        data.extra_light = vec3(2.0, 1.7, 1.2);
    }

    data.n_w = vec3(0);
    data.pos_dither = vec2(0);
    data.pos_ex = vec2(0);
    data.ex = 0.0;

    frag_color = do_light(vec4(data.color, 1.0));
}

void do_model() {
    model_render_data_t rd_model = model_render_data[data.buffer_index];

    data.model_render_flags = rd_model.flags;
    data.pos_dither = vec2(0);
    data.pos_ex = vec2(0);
    data.ex = intBitsToFloat(rd_model.flags); // extra is MRF_*
    data.extra_light = rd_model.extra_light;
    data.extra_bloom = rd_model.extra_bloom;

    if ((rd_model.flags & MRF_ENEMY) != 0) {
        data.extra_light += vec3(0.10);
        data.extra_bloom += vec4(0.15, 0.0, 0.0, 0.15);
    }

    frag_color =
        do_light(
            offset_with_hsva(
                vec4(data.color, 1.0),
                vec4(rd_model.hsv, 0.0)));

    if ((rd_model.flags & MRF_OVERLAY_POST) == 0
        && rd_model.tex_overlay != 0) {
        tex_atlas_entry entry;
        tex_atlas_lookup(rd_model.tex_overlay, tick, entry);

        vec2 coord = uv;

#ifdef SOKOL_MSL
        coord.y = 1.0 - coord.y;
#endif // ifdef SOKOL_MSL

        // in screen pixels
        const vec2 screen_size_px = textureSize(sampler2D(d_color, smp_nearest), 0);
        vec2 v = coord * screen_size_px;

        // TODO: use standard scroll rates
        float scroll = 2 * (tick / 6);
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

        frag_color =
            vec4(
                (overlay_color.a * overlay_color.rgb)
                    + ((1.0 - overlay_color.a) * frag_color.rgb),
                1.0);
    }

    //if ((data.type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_MODEL
    //    && (rd_model.flags & MRF_FIRST_PERSON) != 0
    //    && (rd_model.flags & MRF_FINGER) == 0) {
    //    // turn down default bloom so HUD doesn't get blown out
    //    // TODO
    //    // frag_bloom = vec4(0);

    //    if ((rd_model.flags & MRF_ANY_HAND) != 0) {
    //        const vec3 particle_color = compute_particles_color();
    //        
    //        float intensity = rgb_to_hsv(particle_color).z;
    //        if (intensity > 0.001) {
    //            intensity = saturate(intensity - 0.4);
    //            frag_light += vec4(particle_color, intensity);
    //            frag_bloom += vec4(particle_color, intensity);
    //        }
    //    }
    //}
}

void main() {
    data.extra_light = vec3(0);
    data.extra_bloom = vec4(0);

    // TODO: don't do this in the fragment shader
    mat4 inv_view = inverse(view);

    // sample deferred textures
    const vec4
        s_color = texture(sampler2D(d_color, smp_nearest), uv),
        s_pos_w_id = texture(sampler2D(d_pos_w_id, smp_nearest), uv),
        s_pos_v_index_type_flags =
            texture(sampler2D(d_pos_v_index_type_flags, smp_nearest), uv),
        s_normal_uv = texture(sampler2D(d_normal_uv, smp_nearest), uv);

    // assemble into frag data
    data.color = s_color.rgb;
    data.pos_w = s_pos_w_id.xyz;
    data.pos_w_fake = (inv_view * vec4(s_pos_v_index_type_flags.xyz, 1.0)).xyz;
    data.pos_v = (view * vec4(data.pos_w, 1.0)).xyz;
    data.n_w = (inv_view * vec4(normal_decode(s_normal_uv.xy), 0.0)).xyz;
    data.sprite_size = s_normal_uv.xy;
    data.uv = s_normal_uv.zw;
    data.pos_dither = data.pos_w.xy; // overridden in individual functions
    data.pos_quantized = data.pos_w; // overridden in individual functions
    data.id = floatBitsToInt(s_pos_w_id.w);
    data.ex = 0;

    const int index_type_flags = floatBitsToInt(s_pos_v_index_type_flags.w);
    data.buffer_index = index_type_flags >> 16;
    data.type_flags = index_type_flags & 0xFFFF;
    data.is_sky = (data.type_flags & RENDER_FLAG_SKY) != 0;

    if ((data.type_flags & RENDER_FLAG_SGL) != 0) {
        frag_color = vec4(data.color, 1.0);
        data.ex = 0;
        data.id = 0;
        data.pos_dither = vec2(0);
        frag_color = do_light(frag_color);
    } else if ((data.type_flags & RENDER_FLAG_PARTICLE) != 0) {
        do_particle();
    } else if ((data.type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_MODEL) {
        do_model();
    } else if ((data.id >> 16) == _LT_SIDE) {
        do_side();
    } else if ((data.id >> 16) == _LT_SECTOR) {
        do_sector();
    } else if ((data.id >> 16) == _LT_DECAL) {
        do_decal();
    } else if ((data.type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_SPRITE) {
        do_sprite();
    } 

    //if ((data.model_render_flags & MRF_FIRST_PERSON) == 0) {
    //    vec3 hsv = rgb_to_hsv(frag_color.rgb);
    //    hsv.y -= 0.4;
    //    hsv.z -= 0.1;
    //    frag_color.rgb = hsv_to_rgb(hsv);
    //}

    frag_extra_id_pos = vec4(data.ex, intBitsToFloat(data.id), data.pos_dither);
}
@end

@program program vs fs
