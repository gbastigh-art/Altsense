#ifndef LIGHT_GLSL
#define LIGHT_GLSL

@block light

readonly buffer light_buffer {
    light_t lights[];
};

readonly buffer block_walls_buffer {
    block_wall_data_t block_walls[];
};

readonly buffer block_walls_indices_buffer {
    block_walls_indices_t block_walls_indices[];
};

struct blocks_info {
    ivec2 offset;
    ivec2 size;
};

ivec2 pos_to_block(vec2 point) {
    return ivec2(floor(point.x / BLOCK_SIZE), floor(point.y / BLOCK_SIZE));
}

bool block_present(blocks_info info, ivec2 block) {
    return
        block.x >= info.offset[0]
        && block.y >= info.offset[1]
        && block.x < info.offset[0] + info.size[0]
        && block.y < info.offset[1] + info.size[1];
}

// allows easy DDA through blocks image
// initialize origin, dest, and max_iters
struct blocks_dda_t {
    bool init;
    int iters, max_iters;

    vec2 origin, dest;
    vec2 origin_to_dest;
    float len;
    vec2 dir;

    ivec2 bpos, bstep, bdest;
    vec2 side_dist, delta_dist;

    int block_index;
};

#define BLOCKS_DDA_INIT {                                   \
        false, 0, 0, vec2(0), vec2(0), vec2(0), 0, vec2(0), \
        ivec2(0), ivec2(0), ivec2(0), vec2(0), vec2(0), 0,  \
    }

// DDA from d.origint to d.dest, returns false when out of bounds or done
bool blocks_dda(
        in blocks_info info,
        inout blocks_dda_t d) {
    if (!d.init) {
        d.init = true;

        d.origin_to_dest = d.dest - d.origin,
        d.dir = normalize(d.origin_to_dest);
        d.len = length(d.origin_to_dest);

        if (abs(d.dir.x) < 0.00001f && abs(d.dir.y) < 0.00001f) { return false; }

        const vec2 _from = d.origin.xy / BLOCK_SIZE;
        d.bpos = pos_to_block(d.origin.xy);

        for (int i = 0; i < 2; i++) {
            d.delta_dist[i] =
                abs(d.dir[i]) < 0.0000000001f ? 1e30 : abs(BLOCK_SIZE / d.dir[i]);
            d.side_dist[i] =
                d.delta_dist[i]
                    * (d.dir[i] < 0 ?
                        (_from[i] - d.bpos[i])
                        : (d.bpos[i] + 1.0f - _from[i]));
            d.bstep[i] = d.dir[i] < 0 ? -1 : 1;
        }

        d.bdest = pos_to_block(d.dest.xy);
    } else if (d.iters >= d.max_iters || d.bpos == d.bdest) {
        return false;
    } else {
        // step
        if (d.side_dist.x < d.side_dist.y) {
            d.side_dist.x += d.delta_dist.x;
            d.bpos.x += d.bstep.x;
        } else {
            d.side_dist.y += d.delta_dist.y;
            d.bpos.y += d.bstep.y;
        }
    }

    if (!block_present(info, d.bpos)) { return false; }

    d.block_index =
        ((d.bpos.y - info.offset.y) * info.size.x) + (d.bpos.x - info.offset.x);

    d.iters++;
    return true;
}

// get zs for point, returns sector id -1 if entirely outside of sector
vec2 block_find_zs(
    in blocks_info info,
    vec2 point,
    out int sector_id) {
    sector_id = -1;

    blocks_dda_t dda = BLOCKS_DDA_INIT;
    dda.max_iters = 16;
    dda.origin = point;
    dda.dest = point.xy + (vec2(0, -1) * 100.0);

    while (blocks_dda(info, dda)) {
        // data for nearest side - need to traverse all walls in block to find
        // which is nearest
        float dist = -1.0;
        vec4 p_f = vec4(0), p_c = vec4(0);
        int d_sector = -1;
        bool d_is_bad = false;

        const block_walls_indices_t indices =
            block_walls_indices[dda.block_index];

        for (int n = 0; n < indices.count; n++) {
            const block_wall_data_t data = block_walls[indices.offset + n];

            // check if we hit this line
            float t;
            vec2 hit;
            if (!intersect_segs(data.a, data.b, dda.origin, dda.dest, hit, t)) {
                continue;
            }

            const float d = length(hit - dda.origin);
            if (dist > 0.0 && d >= dist) { continue; }

            // right side normal
            const vec2 nm = vec2(data.b.y - data.a.y, -(data.b.x - data.a.x));
            const float dot_ab = dot(dda.dir.xy, nm);

            // check for the case where we are entering from a back side -
            // that is, we hit a wall where there's nothing FACING the ray
            // (same as left/dot >0 but no right side and vice versa)
            d_is_bad =
                (dot_ab > 0.0 && data.sects[1] == -1)
                || (dot_ab < 0.0 && data.sects[0] == -1);

            // only check sides which are facing INTO our ray - that is,
            // if we dot positive (we are the same direction as the left
            // side), switch to the right and vice versa
            const int which_side = dot_ab > 0.0 ? 1 : 0;

            p_f = data.floors[which_side];
            p_c = data.ceils[which_side];
            dist = d;
            d_sector = data.sects[dot_ab > 0.0 ? 1 : 0];
        }

        if (d_is_bad) {
            sector_id = -1;
            return vec2(0);
        }

        if (dist >= 0.0) {
            sector_id = d_sector;
            return vec2(plane_get_z(p_f, point), plane_get_z(p_c, point));
        }
    }

	return vec2(0);
}

// raycast zs from some level position (origin) to a position (dest) ostensibly
// on a light ranging with dest_zs
// returns true if a wall was hit (hit position and side are written out)
bool block_raycast_zs(
        in blocks_info info,
        vec2 origin,
        vec2 dest,
        vec2 dest_zs,
        float max_distance,
        out vec2 hit_out,
        out int hit_side,
        inout vec2 zs) {
    hit_out = vec2(0);
    hit_side = -1;

    blocks_dda_t dda = BLOCKS_DDA_INIT;
    dda.max_iters = 16;
    dda.origin = origin;
    dda.dest = dest;

    const vec2 zs_init = zs;
    bool stopped = false;

    while (!stopped && blocks_dda(info, dda)) {
        const block_walls_indices_t indices =
            block_walls_indices[dda.block_index];

        for (int n = 0; n < indices.count; n++) {
            const block_wall_data_t data = block_walls[indices.offset + n];

            // no lines can be skipped according to their normal - some lines
            // have different mids on both sides

            // check if we hit this line
            float t = 0.0;
            vec2 hit = vec2(0);
            if (!intersect_segs(data.a, data.b, origin.xy, dest.xy, hit, t)) {
                continue;
            }

            const float dist = length(hit - origin);
            if (dist > max_distance) { continue; }

            // z directions for z0, z1 (dz0, dz1)
            // NOTE: bottom goes to top and top goes to bottom!
            // this is why dest_zs are flipped
            // also compute ray z0/z1 at point of contact (rz0/1)
            const float
                dz0 = (dest_zs[1] - zs.x) / dda.len,
                dz1 = (dest_zs[0] - zs.y) / dda.len,
                rz0 = zs.x + (dz0 * dist),
                rz1 = zs.y + (dz1 * dist);

            // check planes on both sides of portal
            for (int i = 0; i < 2; i++) {
                // check that side id != -1 (side is present)
                if (data.sides[i] == -1) { continue; }

                // pass through portal, clamp zs
                const float
                    t_side = i == 1 ? (1.0 - t) : t,
                    mzbl = data.zbls[i],
                    mzbr = data.zbrs[i],
                    mztl = data.ztls[i],
                    mztr = data.ztrs[i],
                    z0 = mix(mzbl, mzbr, t_side),
                    z1 = mix(mztl, mztr, t_side);

                // if zeros (wall) or no overlap, then this is entirely out
                // of view
                if ((abs(z0) < 0.0001 && abs(z1) < 0.0001)
                    || rz0 > z1
                    || rz1 < z0) {
                    hit_out = hit;
                    hit_side = data.sides[i];
                    max_distance = dist;
                    stopped = true;
                }

                if (rz0 < z0) {
                    const float slope =
                        (z0 - dest_zs[0]) / length(dest - hit);
                    zs.x = max(dest_zs[0] + (slope * dda.len), zs.x);
                }

                if (rz1 > z1) {
                    const float slope =
                        (z1 - dest_zs[1]) / length(dest - hit);
                    zs.y = min(dest_zs[1] + (slope * dda.len), zs.y);
                }
            }
        }
    }

	return stopped;
}

#define NUM_POISSON 32

const vec2 POISSON[32] = {
    { -0.94201624,  -0.39906216 },
    {  0.94558609,  -0.76890725 },
    { -0.094184101, -0.92938870 },
    {  0.34495938,   0.29387760 },
    { -0.91588581,   0.45771432 },
    { -0.81544232,  -0.87912464 },
    { -0.38277543,   0.27676845 },
    {  0.97484398,   0.75648379 },
    {  0.44323325,  -0.97511554 },
    {  0.53742981,  -0.47373420 },
    { -0.26496911,  -0.41893023 },
    {  0.79197514,   0.19090188 },
    { -0.24188840,   0.99706507 },
    { -0.81409955,   0.91437590 },
    {  0.19984126,   0.78641367 },
    {  0.14383161,  -0.14100790 },
    {  0.94201624,   0.39906216 },
    { -0.94558609,   0.76890725 },
    {  0.094184101,  0.92938870 },
    { -0.34495938,  -0.29387760 },
    {  0.91588581,  -0.45771432 },
    {  0.81544232,   0.87912464 },
    {  0.38277543,  -0.27676845 },
    { -0.97484398,  -0.75648379 },
    { -0.44323325,   0.97511554 },
    { -0.53742981,   0.47373420 },
    {  0.26496911,   0.41893023 },
    { -0.79197514,  -0.19090188 },
    {  0.24188840,  -0.99706507 },
    { -0.81409955,  -0.91437590 },
    { -0.19984126,  -0.78641367 },
    { -0.14383161,   0.14100790 },
};

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

vec3 light_get_shadow_pos(vec3 pos, bool is_sector) {
    const vec3 sp = floor(pos * PX_PER_UNIT) / PX_PER_UNIT;
    return vec3(sp.xy, pos.z);
}

vec2 sign2(vec2 v) {
    return vec2(sign(v.x), sign(v.y));
}

vec3 light_contribution(
        in light_t l,
        texture2D shadow_image,
        sampler smp,
        in composite_frag_data data,
        int tick,
        int visopt) {
        // vec3 pos_w,
        // vec3 n_w,
        // vec2 pos_dither,
        // vec3 pos_quantized,
    vec3 pos_w = data.pos_w;
    vec3 n_w = data.n_w;
    vec2 pos_dither = data.pos_dither;
    vec3 pos_quantized = data.pos_quantized;

    // if light does not have no shadows (has shadows), quantize pos for shadows
    if ((l.flags & LIGHT_FLAG_NO_SHADOWS) == 0) {
        vec3 shadow_pos =
            light_get_shadow_pos(
                l.pos, (l.flags & LIGHT_FLAG_SECTOR) != 0);
        l.pos = shadow_pos;
    }

    vec3
        l_frag_w = pos_quantized,
        frag_to_l = l.pos - l_frag_w;

    float dist = length(frag_to_l);
    if (dist > l.attenuation + l.span) { return vec3(0); }

    const vec3 dir_l = normalize(frag_to_l);

    // shadow, power
    float s = 0.0, power = 0.0;

    if ((l.flags & LIGHT_FLAG_NO_SHADOWS) == 0) {
        // offset shadow world position with normal (just a bit)
        const vec3 l_frag_w_shadow =
            l_frag_w + (n_w * ((l.attenuation + l.span) / 8.0) * 0.15);

        // find our coord relative to light
        const vec2 coord =
            ((l_frag_w_shadow.xy - l.pos.xy) / (l.attenuation + l.span)) * 0.5;

        // uv in [0,1]^2 to sample light shadow image
        const vec2 uv = clamp(vec2(0.5) + coord, vec2(0), vec2(1));

        const int shadow_map_size_lights =
            SHADOW_MAP_SIZE / SHADOW_MAP_PER_LIGHT;

        const vec2
            simage_unit = 1.0 / textureSize(sampler2D(shadow_image, smp), 0),
            simage_light_unit =
                vec2(SHADOW_MAP_PER_LIGHT / float(SHADOW_MAP_SIZE)),
            shadow_image_offset =
                vec2(
                    l.shadow_index % shadow_map_size_lights,
                    l.shadow_index / shadow_map_size_lights)
                        * simage_light_unit;
        
        if ((visopt & _VISOPT_NODANCE) == 0) {
            pos_dither +=
                vec2(
                    ((tick + 0) / 10) % 3,
                    ((tick + 40) / 10) % 3)
                        * (1.0 / PX_PER_UNIT);
        }

        // pos dither which matches up with pixel position
        const ivec2 pos_dither_i = ivec2(floor(pos_dither.xy * PX_PER_UNIT));
        const vec2 pos_dither_center = vec2(pos_dither_i) + vec2(0.5);

        // used for both possion and dither indexing
        const int dither_index = ((pos_dither_i.y % 8) * 8) + (pos_dither_i.x % 8);

        // sample shadow image for zs (rg) and power (b)
        vec2 shadow_coord =  shadow_image_offset;

        // offset into light
        shadow_coord += (uv * simage_light_unit);

        // dither offset
        if (dist > (l.span / 2.0)) {
            shadow_coord += POISSON[dither_index % NUM_POISSON] * simage_unit;
        }

        const vec4 v = texture(sampler2D(shadow_image, smp), shadow_coord);

        if (l_frag_w.z < v.x || l_frag_w.z > v.y) {
            s += 1.0;
        } else {
            power += v.b;
        }

        // hard cutoff at s == 0.5
        s = s > 0.5 ? 1.0 : 0.0;
    } else {
        s = 0.0;
        power = 1.0 - (dist / (l.attenuation + l.span));
    }

    float
        dist_normalized = max(dist / (l.attenuation + l.span), 0.00001),
        factor = 0.0;

    const int
        surface_type = (data.id >> 16) & 0xFFFF,
        surface_index = (data.id >> 0) & 0xFFFF,
        light_type = LIGHT_ID_TYPE(l.id),
        light_index = LIGHT_ID_INDEX(l.id);

    if (n_w == vec3(0.0)) {
        factor = 1.0;
    } else if (
        ((surface_type == _LT_SIDE && light_type == LIGHT_TYPE_SIDE)
            || (surface_type == _LT_SECTOR && light_type == LIGHT_TYPE_FLOOR)
            || (surface_type == _LT_SECTOR && light_type == LIGHT_TYPE_CEIL))
        && light_index == surface_index) {
        factor = 1.0;
        s = 0.0;
    } else if (power >= 1.0) {
        factor = 1.0;
    } else if ((l.flags & (LIGHT_FLAG_SIDE | LIGHT_FLAG_SECTOR)) != 0) {
        if ((l.flags & LIGHT_FLAG_SIDE) != 0) {
            const vec3
                tangent = vec3(rotate_vec2(l.plane.xy, PI_2), 0),
                a = l.pos + (tangent * (l.span / 2.0)),
                b = l.pos - (tangent * (l.span / 2.0)),
                p = point_project_segment(pos_quantized, a, b);

            const float angle = _atan2(l.plane.y, l.plane.x);
            const float bias = abs((angle / PI_2) - (round(angle / PI_2)));

            if (length(p.xy - pos_w.xy) < 0.01 + 0.05 * bias) {
                factor = 1.0;
            } else {
                const vec3 d = normalize(p - pos_quantized);
                factor = max(dot(d, n_w), 0.0);
            }
        } else {
            factor = max(dot(dir_l, n_w), 0.0);
        }
    } else {
        factor = max(dot(dir_l, n_w), 0.0);
    }

    if ((l.flags & LIGHT_FLAG_IGNORE_NEAR) != 0) {
        factor = mix(1.0, factor, clamp(dist / 1.5, 0.0, 1.0));
    }

    // mix with 2^(10x-10) to ensure a falloff near the edge of the light bounds
    // (equation is basically 0 until 0.9 where it goes steeply to 1.0)
    const float falloff = pow(2, (10 * dist_normalized) - 10);

    // scale power by z distance if z_attenuation is applied
    float z_power = 1.0;
    if (abs(l.z_attenuation) > 0.0001) {
        float z_inv_power =
            clamp(abs(frag_to_l.z) / l.z_attenuation, 0.0, 1.0);

        z_power =
            mix(
                1.0 / (1.0 + (l.c1 * z_inv_power) + (l.c2 * pow(z_inv_power, 2))),
                0.0,
                z_inv_power);
    }

    const float
        inv_power = 1.0 - power,
        curved_0 = 1.0 / (1.0 + (l.c1 * inv_power) + (l.c2 * pow(inv_power, 2))),
        curved_1 = mix(curved_0 * z_power, 0.0, falloff);

    const vec3 color = l.color + (1.0 * l.power);
    vec3 result = (1.0 - s) * color * factor * curved_1;
    result += (1.0 - falloff) * (1.0 - dist_normalized) * l.ambient * color;
    return result;
}

vec3 light_frag(
        texture2D shadow_image,
        sampler smp,
        in composite_frag_data data,
        int tick,
        int visopt) {
    vec3 result = vec3(0.0);

    int i = 0;
    while (i < MAX_LIGHTS) {
        light_t l = lights[i];
        if (l.flags == -1) { break; } 

        i++;

        result +=
            light_contribution(
               l,
               shadow_image,
               smp,
               data,
               tick,
               visopt);
    }

    return result;
}

@end

#endif // ifndef LIGHT_GLSL
