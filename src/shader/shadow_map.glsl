@module shadow_map

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
@include_block light

in vec2 uv;

uniform fs_params {
    vec3 l_pos;
    float l_attenuation;
    float l_span;

    // if non-negative, then this is a plane light for the specified sector ID
    int l_sector;

    // if non-negative, then this is a side light for the specified sector ID
    int l_side;

    // plane of light if l_sector or l_side
    vec4 l_plane;

    // light zs
    vec2 l_zs;

    ivec2 blocks_offset;
    ivec2 blocks_size;

    // lines to clamp to, if l_sector or l_side (1 line)
    int n_l_lines;
    vec4 l_lines[MAX_LIGHT_LINES];
};

void raycast_zs_for_lines(
        in blocks_info info,
        vec2 dest, float z0, float z1,
        float attenuation,
        inout vec2 zs,
        out float factor) {
    const vec2 zs_init = zs;

    // closest visible point which did not hit a wall
    vec2 p_closest = l_lines[0].xy;

    // zs for closest point
    vec2 zs_closest = vec2(-1e10f);

    // distance to closest point
    float dist_closest = 1e10f;

    vec2 last = vec2(-1);

    vec2 hit = vec2(0);
    vec2 zs_working = zs_init;
    int hit_side = -1;

    // first check center for sector lights
    if (l_sector != -1) {
        block_raycast_zs(
            info,
            dest, l_pos.xy, vec2(z0, z1),
            length(dest - l_pos.xy) + 0.001,
            hit, hit_side, zs_working);

        // if we hit something, not visible
        if (hit_side == -1 || (l_side != -1 && hit_side == l_side)) {
            dist_closest = length(dest - l_pos.xy);
            zs_closest = zs_working;
            p_closest = l_pos.xy;
        }
    }

    for (int i = 0; i < n_l_lines; i++) {
        // TODO: should endpoints be checked as well? seems to cause bugs.
        vec2 ps[2] = {
            point_project_segment(dest, l_lines[i].xy, l_lines[i].zw),
            mix(l_lines[i].xy, l_lines[i].zw, 0.5),
        };

        for (int j = 0; j < 2; j++) {
            if (length(ps[j] - last) < 0.001) { continue; }

            last = ps[j];
            vec2 p = ps[j];

            if (l_sector != -1) {
                // attract slightly towards center
                if (p != l_pos.xy) {
                    p += normalize(l_pos.xy - p) * 0.00001;
                }
            } else if (l_side != -1) {
                p += l_plane.xy * 0.01;
            }

            const float dist = length(p - dest);
            if (dist > dist_closest) {
                continue;
            }

            block_raycast_zs(
                info,
                dest, p, vec2(z0, z1),
                dist + 0.001,
                hit, hit_side, zs_working);

            // if we hit something, not visible
            if (hit_side != -1 && (l_side == -1 || hit_side != l_side)) {
                continue;
            }

            dist_closest = dist;
            zs_closest = zs_working;
            p_closest = p;
        }
    }

    zs = zs_closest;

    // clamp to plane iff sector
    if (l_sector != -1) {
        for (int i = 0; i < 2; i++) {
            if (plane_classify(l_plane, vec3(dest, zs[i])) < 0) {
                zs[i] = plane_get_z(l_plane, dest);
            }
        }
    }

    factor = 1.0 - (length(dest - p_closest) / attenuation);
}

layout (location=0) out vec4 frag_zs;

void main() {
    blocks_info info;
    info.offset = ivec2(blocks_offset[0], blocks_offset[1]);
    info.size = ivec2(blocks_size[0], blocks_size[1]);

    vec2 hit;
    vec2 zs = vec2(0, 0);

    vec3 shadow_pos = light_get_shadow_pos(l_pos, l_sector != 0);

    vec2 coord = uv - 0.5;
    vec2 dir = normalize(coord);
    vec2 dest = shadow_pos.xy + coord * (2.0 * (l_attenuation + l_span));

    // throw out pixels which are entirely outside of the l_attenuation + l_span
    // range (this is because lights are circles, we have some dead pixels for
    // each light)
    if (length(dest - shadow_pos.xy) > l_attenuation + l_span) {
        frag_zs = vec4(-1e10f);
        return;
    }

    // find starting zs, sector
    int sector_id;
    zs = block_find_zs(info, dest, sector_id);

    // no need to light outside of sectors
    if (sector_id == -1) {
        frag_zs = vec4(-1e10f);
        return;
    }

    // we can immediately throw away a lot of points if we're a side, anything
    // behind us is not lit
    if (l_side != -1 && plane_classify(l_plane, vec3(dest, 0)) < 0.0) {
        frag_zs = vec4(-1e10f);
        return;
    }

    // cast ray towards light from destination
    float factor;
    if (l_side != -1 || (l_sector != -1 && sector_id != l_sector)) {
        // sector/side zs
        raycast_zs_for_lines(
            info,
            dest, l_zs.x, l_zs.y,
            l_attenuation, zs, factor);
    } else if (l_sector != -1) {
        factor = 1.0;
    } else {
        // point light
        int hit_side = -1;
        block_raycast_zs(
            info,
            dest, shadow_pos.xy, vec2(shadow_pos.z),
            length(dest - shadow_pos.xy),
            hit, hit_side, zs);

        if (hit_side != -1) { zs = vec2(-1e10f); }

        factor = 1.0 - (length(dest - shadow_pos.xy) / l_attenuation);
    }

    frag_zs = vec4(zs, factor, 1.0);
}
@end

@program program vs fs
