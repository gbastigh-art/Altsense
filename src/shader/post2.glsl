@module post2

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
@include_block common

uniform sampler smp_nearest;
uniform sampler smp_linear;

@include_block tex_atlas

uniform texture2D base_image;
uniform texture2D light_image;
uniform texture2D bloom_image;
uniform texture2D blur_image;
uniform texture2D d_pos_v_index_type_flags;
uniform texture2D c_extra_id_pos;
uniform texture3D palette_generic;
uniform texture2D phantom_image;
uniform texture2D edge_image;

uniform fs_params {
    mat4 view, inv_view;
    vec3 cam_pos;
    int tick;
    float time_s;
    int visopt;
    int highlight_id;
    int changed_id;
    float speed_effect;
    float liquid_fall_effect;
    int liquid_fall_tex0;
    int liquid_fall_tex1;
    float liquid_fall_tex_mix;
    float liquid_fall_tex_alpha;
    int hit_tick;
    float contrast_effect;
    vec4 phantom_color;
    int transition_tex;
    float transition_effect;
    float last_heartbeat_s;
    int vignette_tex0, vignette_tex1;
    float vignette_tex_mix;
    vec4 vignette_color;
    vec4 vignette_tex_color;
    float vignette_strength;
    int no_dither256;
    int no_bloom;
};

in vec2 uv;

layout(location=0) out vec4 frag_color;

// adapted from https://www.shadertoy.com/view/4dSyWK
#define SPEED_LINE_SPEED 3.0
#define SPEED_LINE_RADIUS 4.0
#define SPEED_LINE_EDGE 0.5
#define SPEED_LINE_SIZE 0.5

float speed_line(in vec2 uv, in vec2 resolution, float effect) {
    const float
        speed = SPEED_LINE_SPEED * mix(1.0, 1.25, effect),
        radius = SPEED_LINE_RADIUS,
        edge = SPEED_LINE_EDGE;

	vec2 p = (uv * resolution) / resolution.y;
    float aspect = resolution.x / resolution.y;
    vec2 pos_c = p - vec2(0.5 * aspect, 0.5);

    p = vec2(0.5 * aspect, 0.5) + normalize(pos_c) * min(length(pos_c), 0.05);

    // noise
    vec3 p3 =
        (1.0 / (SPEED_LINE_SIZE * 0.025))
            * 0.25
            * vec3(p, (30 + (tick / float(TICKS_PER_SECOND))) * 0.004);

    const float
        noise = 0.5 + 0.5 * simplex_3d(p3 * 32.0),
        dist_c = clamp((length(pos_c)) / radius, 0.0, 1.0) * noise,
        falloff = 1.0 - pow(abs(((2.0 * dist_c) - 1.0)), 4.0);

    // threshold
    return smoothstep(edge, edge + 0.1, noise * falloff) > 0.5 ? 1.0 : 0.0;
}

vec3 dither_and_palettize(vec3 rgb, ivec2 pos_px) {
    if (no_dither256 != 0) { return rgb; }

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

    const int d_index = ((pos_px.y % 8) * 8) + (pos_px.x % 8);
    const float d_offs =
        ((DITHER_MATRIX[d_index] / 64.0) - 0.5) * (1.0 / 20.0);
    return
        texture(
            sampler3D(palette_generic, smp_nearest),
            saturate(rgb + d_offs)).rgb;
}

vec3 do_liquid_fall_effect(in vec2 uv, in vec2 size) {
    vec2 coord = uv;
    coord *= 4.0;
    float n = saturate(fbm(vec3(coord.x, coord.y + (time_s * 16.0), time_s * 4.0)));
    n = ease_in_out_quart(n);
    n = clamp(n, 0.1, 0.9);
    
    const vec3 c0 = vec3(0.1, 0.0, 0.0), c1 = con_sat_brt(vec3(0.8, 0.5, 0.4), 1.0, 1.0, 0.6);
    const vec3 c0_xyz = rgb_to_xyz(c0), c1_xyz = rgb_to_xyz(c1);

    vec3 rgb = xyz_to_rgb(mix(c0_xyz, c1_xyz, n));
    
    vec3 s_blur = texture(sampler2D(blur_image, smp_linear), uv).rgb;
    {
        vec3 hsv = rgb_to_hsv(s_blur);
        hsv.z -= 0.1;
        s_blur = hsv_to_rgb(hsv);
    }
    rgb = mix(rgb, s_blur, 0.5);

    if (liquid_fall_tex0 != 0 || liquid_fall_tex1 != 0) {
        tex_atlas_entry entry0, entry1;
        tex_atlas_lookup(liquid_fall_tex0, tick, entry0);
        tex_atlas_lookup(liquid_fall_tex1, tick, entry1);

        vec2 coord = uv;

#ifdef SOKOL_MSL
        coord.y = 1.0 - coord.y;
#endif // ifdef SOKOL_MSL

        // in screen pixels
        vec2 v = coord * size;

        // TODO: use standard scroll rates
        float scroll = tick / 6;

        if (true) {
            v.x += scroll;
        }

        if (true) {
            v.y -= scroll;
        }

        // screen pixels -> tex atlas uv units
        v *= 1.0 / float(TEX_ATLAS_SIZE);

        vec4 overlay_color0 =
            tex_atlas_sample(vec3(entry0.mi + mod(v, entry0.size), entry0.layer));
        vec4 overlay_color1 =
            tex_atlas_sample(vec3(entry1.mi + mod(v, entry1.size), entry1.layer));
        vec4 overlay_color =
            mix(overlay_color0, overlay_color1, liquid_fall_tex_mix);

        rgb = mix(rgb, overlay_color.rgb, liquid_fall_tex_alpha * overlay_color.a);
    }

    rgb = dither_and_palettize(rgb, ivec2(uv * size));
    return rgb;
}

vec4 do_transition_effect(in vec2 uv, in vec2 size) {
    const float effect = transition_effect;
    vec2 coord = uv;
    float n = saturate(fbm(vec3(coord.x * 24.0, coord.y - (time_s * 16.0), time_s * 4.0)));
    n = ease_in_out_quart(n);

    tex_atlas_entry entry;
    tex_atlas_lookup(transition_tex, tick, entry);

    // background texture
    vec3 rgb = vec3(0);
    {
        vec2 tex_coord = uv;

#ifdef SOKOL_MSL
        tex_coord.y = 1.0 - tex_coord.y;
#endif // ifdef SOKOL_MSL

        // in screen pixels
        vec2 v = tex_coord * size;

        float scroll = tick / 6;
        v.x += scroll;
        v.y -= scroll;

        // screen pixels -> tex atlas uv units
        v *= 1.0 / float(TEX_ATLAS_SIZE);

        vec4 s = tex_atlas_sample(vec3(entry.mi + mod(v, entry.size), entry.layer));
        rgb = mix(rgb, s.rgb, 0.2 * s.a);
    }

    const float strength = ((1.0 - (length(uv - vec2(0.5)) / (1.0f / sqrt(2)))) + (n * 0.05));
    vec4 result =
        vec4(
            rgb,
            strength < effect ? 1.0 : (1.0 - saturate((strength - effect) / 0.4)));

    // cenetered texture
    {
        // compute texture size in pixels
        const vec2 entry_size_px = entry.size / (1.0f / float(TEX_ATLAS_SIZE));
        const vec2 offs = vec2((size - ivec2(entry_size_px)) / 2);

        vec2 px = uv;
#ifdef SOKOL_MSL
        px.y = 1.0 - px.y;
        px *= size;
#endif // ifdef SOKOL_MSL

        const vec2 pos_s =
            ((ivec2(px) - offs) * (1.0f / float(TEX_ATLAS_SIZE))) / vec2(entry.size);

        if (pos_s.x > 0.0 && pos_s.y > 0.0 && pos_s.x < 1.0 && pos_s.y < 1.0) {
            const vec4 s_rgba =
                tex_atlas_sample(
                    vec3(entry.mi + (pos_s * entry.size), entry.layer));
            const vec3 s_hsv = rgb_to_hsv(s_rgba.rgb);
            const float intensity = s_hsv.z;

            vec3 tex_rgb =
                s_hsv.z
                    * clamp(
                        fbm(vec3(uv * 16.0, time_s * 20.0)),
                        0.5,
                        1.0)
                    * vec3(1.0, 0.5, 0.3);

            result.rgb = mix(result.rgb, tex_rgb, s_rgba.a);
            result.a = saturate(result.a + s_rgba.a);
            result.a *= 1.0 - saturate((0.2 - (effect - 0.1)) / 0.2);
        }
    }

    return result;
}

vec4 do_vignette_effect(in vec2 uv, in vec2 size) {
    float n = saturate(fbm(vec3(uv * 16.0, time_s * 12.0)));
    n = ease_in_out_quart(n);

    // compute distance to edge here
    vec2 px = uv;
#ifdef SOKOL_MSL
        px.y = 1.0 - px.y;
        px *= size;
#endif // ifdef SOKOL_MSL

    float px_to_edge = size.x;
    px_to_edge = min(px_to_edge, abs(px.x - 0));
    px_to_edge = min(px_to_edge, abs(px.x - size.x));
    px_to_edge = min(px_to_edge, abs(px.y - 0));
    px_to_edge = min(px_to_edge, abs(px.y - size.y));

    float strength = max(20 - px_to_edge, 0) / 20.0;

    const float beat = 1.0 - max(time_s - last_heartbeat_s, 0);

    // add to strength according to rounded center distance for nicer corners
    const float dist_to_center = (length(uv - vec2(0.5))) / (1.0 / sqrt(2));
    strength += 1.3 * pow(dist_to_center, 9.0);
    strength += (2.0 * (n - 0.5)) * 0.2;

    strength = saturate(strength);
    strength *= vignette_strength;

    strength += mix(0.0, 0.5, vignette_strength) * (1.5 * beat);

    // no vignette in center, start at ~0.6 distance
    strength *= saturate(max(dist_to_center - 0.6, 0.0) / 0.1);

    // background texture
    vec4 rgba = vec4(vignette_color.rgb, 1);
    {
        tex_atlas_entry entry0, entry1;
        tex_atlas_lookup(vignette_tex0, tick, entry0);
        tex_atlas_lookup(vignette_tex1, tick, entry1);

        vec2 tex_coord = uv;

#ifdef SOKOL_MSL
        tex_coord.y = 1.0 - tex_coord.y;
#endif // ifdef SOKOL_MSL

        // in screen pixels
        vec2 v = tex_coord * size;

        float scroll = tick / 6;
        v.x += scroll;
        v.y -= scroll;

        // screen pixels -> tex atlas uv units
        v *= 1.0 / float(TEX_ATLAS_SIZE);

        vec4 s0, s1;
        s0 = tex_atlas_sample(vec3(entry0.mi + mod(v, entry0.size), entry0.layer));
        s1 = tex_atlas_sample(vec3(entry1.mi + mod(v, entry1.size), entry1.layer));
        vec4 s = mix(s0, s1, vignette_tex_mix);
        rgba.a -= vignette_tex_color.a * (1.0 - s.a);
        rgba.rgb += s.rgb * vignette_tex_color.rgb;
        //rgb = mix(rgb, s.rgb, 0.2 * s.a);
    }

    if (strength > 0) {
        return vec4(rgba.rgb, mix(0.0, 0.25 + (0.03 * beat), strength) * vignette_color.a);
    } else {
        return vec4(1, 1, 1, 0);
    }
}

void main() {
    const vec2 size = textureSize(sampler2D(base_image, smp_nearest), 0);

    const vec4 s_pos_v_index_type_flags =
        texture(sampler2D(d_pos_v_index_type_flags, smp_nearest), uv);

    const vec4 s_extra_id_pos =
        texture(sampler2D(c_extra_id_pos, smp_nearest), uv);

    const vec3 pos_v = s_pos_v_index_type_flags.xyz;

    const vec3 pos_w_fake =
        (inv_view * vec4(s_pos_v_index_type_flags.xyz, 1.0)).xyz;

    const int type_flags = floatBitsToInt(s_pos_v_index_type_flags.w) & 0xFFFF;
    const int render_type = type_flags & RENDER_TYPE_MASK;
    const int index = floatBitsToInt(s_pos_v_index_type_flags.w) >> 16;

    frag_color = vec4(texture(sampler2D(base_image, smp_nearest), uv).rgb, 1.0);

    // add edges
    const float s_edge = texture(sampler2D(edge_image, smp_nearest), uv).r;
    frag_color.rgb = mix(frag_color.rgb, vec3(0.9, 0.6, 0.2), 0.25 * s_edge);

    if (hit_tick != 0 && tick - hit_tick <= 20) {
        vec3 hsv = rgb_to_hsv(frag_color.rgb);
        const vec3 val_rgb = hsv_to_rgb(vec3(0, 0, ease_in_out_quart(hsv.z)));

        float since = saturate((tick - hit_tick) / float(20));
        since = 1.0 - since;
        frag_color.rgb = mix(frag_color.rgb, val_rgb, since);
    }
 
    const vec2 aberrated =
        vec2(1.5) * (1.0 / textureSize(sampler2D(base_image, smp_nearest), 0));
	const vec4 frag_color_a =
        vec4(
            texture(sampler2D(base_image, smp_nearest), uv - aberrated).r,
            texture(sampler2D(base_image, smp_nearest), uv).g,
            texture(sampler2D(base_image, smp_nearest), uv + aberrated).b,
            1.0);

    const vec4 bloom = texture(sampler2D(bloom_image, smp_linear), uv);
    const vec3 light = texture(sampler2D(light_image, smp_nearest), uv).rgb;

    const float scaled_dist = pow(2, (10.0 * saturate(-pos_v.z / 24.0)) - 10.0);
    float ab_factor = 0.0;
    ab_factor += 0.6 * scaled_dist;
    ab_factor += 0.1 * (abs(dot(light, vec3(1))) / 3.0);
    ab_factor += 1.2 * saturate(bloom.a - 0.3);
    ab_factor += ((type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_MODEL) ? 1.0 : 0.0;
    ab_factor = saturate(ab_factor);
    // ab_factor += s_edge;
    // frag_color.rgb = mix(frag_color.rgb, vec3(0.9, 0.6, 0.2), 0.33 * s_edge);

    if (no_bloom == 0) {
        frag_color.rgb = mix(frag_color.rgb, frag_color_a.rgb, ab_factor);
    }

    if ((visopt & _VISOPT_VALUE) != 0) {
    	frag_color.rgb = rgb_to_hsv(frag_color.rgb).zzz;
    }

    if (liquid_fall_effect > 0.0f) {
        float power = liquid_fall_effect;

        if ((type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_MODEL) {
            const int model_flags = floatBitsToInt(s_extra_id_pos.x);
            if ((model_flags & MRF_FIRST_PERSON) != 0) {
                power *= 0.15;
            }
        }

        const vec3 ee = do_liquid_fall_effect(uv, size);
        frag_color.rgb = mix(frag_color.rgb, ee, 0.9 * power); 
    }

    const vec3 speed_color =
        vec3(
            speed_line(uv - aberrated, size, speed_effect),
            speed_line(uv, size, speed_effect),
            speed_line(uv + aberrated, size, speed_effect));
    frag_color.rgb += 0.2 * saturate(speed_effect) * speed_color;

    const vec4 s_phantom =
        texture(sampler2D(phantom_image, smp_nearest), uv);
    frag_color.rgb =
        mix(
            frag_color.rgb,
            phantom_color.rgb,
            mix(0.0, phantom_color.a, s_phantom.a));

    // transition overlay
    if (transition_effect > 0.0) {
        const vec4 col_tr = do_transition_effect(uv, size);
        frag_color.rgb = mix(frag_color.rgb, col_tr.rgb, col_tr.a);
    }

    // vignette effect
    {
        const vec4 col_vg = do_vignette_effect(uv, size);
        frag_color.rgb = mix(frag_color.rgb, col_vg.rgb, col_vg.a);
    }

    frag_color.rgb = dither_and_palettize(frag_color.rgb, ivec2(uv * size));

    // contrast
    {
        frag_color.rgb =
            mix(
                frag_color.rgb,
                con_sat_brt(frag_color.rgb, 2.0, 0.001, 10.0),
                contrast_effect);

        // frag_color.rgb =
        //     mix(
        //         frag_color.rgb,
        //         con_sat_brt(frag_color.rgb, 2.0, 0.001, 10.0),
        //         1.0);
    }

    if ((type_flags & RENDER_TYPE_MASK) == RENDER_TYPE_SECTOR
        && (visopt & _VISOPT_SHOWSECT) != 0) {
        int seed = index & 0xFFFF;
        // must match lptr_rand_abgr
        vec3 col =
            vec3(
                ((seed * 37) & 0xFF) / 255.0,
                ((seed * 17) & 0xFF) / 255.0,
                ((seed * 67) & 0xFF) / 255.0);
        col = normalize(col);
        frag_color.rgb = mix(frag_color.rgb, col, 0.5);
    }

    if ((visopt & _VISOPT_HIGHLIGHT_3D) != 0) {
        const int id = floatBitsToInt(s_extra_id_pos.y);

        if (id != 0 && id == highlight_id) {
            frag_color.rgb +=
                vec3(0.15 * abs(sin((tick / float(TICKS_PER_SECOND)) * 2.0)));
        }

        if (id != 0 && id == changed_id) {
            frag_color.rgb = mix(frag_color.rgb, vec3(1.0, 0.6, 0.03), 0.1);
        }
    }
}

@end

@program program vs fs
