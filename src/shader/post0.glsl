@module post0

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

@include_block common

uniform texture2D d_pos_v_index_type_flags;
uniform texture2D c_color;
uniform texture2D c_extra_id_pos;
uniform texture2D c_light;
uniform texture2D c_bloom;

readonly buffer particle_buffer {
    gpu_psim_particle_t particles[];
};

readonly buffer cell_buffer {
    gpu_psim_cell_t cells[];
};

uniform fs_params {
    int tick;
    float time_s;
    int visopt;

    int n_particles;
    float particle_kern_radius;
    float particle_disp_scale;
    float particle_disp_radius;
    vec2 particle_bounds;
    vec2 particle_render_offset;

    vec3 particle_color_left;
    vec3 particle_color_right;

    ivec2 particle_cell_dims;

    vec4 override_color;
};

in vec2 uv;

layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_light;
layout(location=2) out vec4 frag_bloom;

#define PI 3.14159265359

float k_poly6(float d, float r) {
    if (d > r) { return 0.0; }
    const float v = ((r * r) - (d * d));
    const float poly6 = (4.0 / (PI * pow(r, 8.0)));
    return v * v * v * poly6;
}

int psim_index_from_cell(ivec2 cell) {
    return (cell.y * particle_cell_dims.x) + cell.x;
}

ivec2 psim_cell_from_pos(vec2 pos) {
    // offset by 1 since cell bounds include one boundary cell on each side
    return ivec2(pos * (1.0f / particle_kern_radius)) + ivec2(1);
}

vec3 compute_particles_color() {
    vec2 pos = vec2(uv.x, 1.0 - uv.y) * particle_bounds;
    pos -= particle_render_offset;

    float weights[2];
    weights[0] = 0.0;
    weights[1] = 0.0;
    float density = 0.0;

    const ivec2 neighbors[] = {
        ivec2(-1, -1),
        ivec2(0,  -1),
        ivec2(1,  -1),
        ivec2(-1, 0),
        ivec2(0,  0),
        ivec2(1,  0),
        ivec2(-1, 1),
        ivec2(0,  1),
        ivec2(1,  1),
    };

    const ivec2 cell_org = psim_cell_from_pos(pos);

    for (int n = 0; n < 9; n++) {
        const ivec2 cell_n = cell_org + neighbors[n];
        const gpu_psim_cell_t cell = cells[psim_index_from_cell(cell_n)];

        if (cell_n.x < 0
            || cell_n.y < 0
            || cell_n.x >= particle_cell_dims.x
            || cell_n.y >= particle_cell_dims.y) {
            return vec3(1, 0, 1);
        }

        // INT_MAX -> no entries
        if (cell.start == 2147483647) {
            continue;
        } else if (cell.count < 0
            || cell.count > 2048
            || cell.start < 0
            || cell.start > 2048) {
            return vec3(1, 0, 1);
        }

        int i = cell.start, end = cell.start + cell.count;
        while (i < end) {
            const float dist = length(particles[i].pos - pos);
            if (dist < particle_disp_radius) {
                const float poly6 = k_poly6(dist, particle_disp_radius);
                density += particles[i].density * poly6;

                weights[0] += (1 - particles[i].is_right) * poly6;
                weights[1] += particles[i].is_right * poly6;
            }

            i++;
        }
    }

    const float t = saturate(density / particle_disp_scale);
    return
        hsv_to_rgb(
            mix(
                rgb_to_hsv(vec3(0)),
                rgb_to_hsv(
                    mix(
                        particle_color_left,
                        particle_color_right,
                        weights[1] / (weights[0] + weights[1]))),
                t));
}

void main() { 
    const vec4
        s_pos_v_index_type_flags =
            texture(sampler2D(d_pos_v_index_type_flags, smp_nearest), uv),
        s_extra_id_pos =
            texture(sampler2D(c_extra_id_pos, smp_nearest), uv),
        s_color = texture(sampler2D(c_color, smp_nearest), uv),
        s_light = texture(sampler2D(c_light, smp_nearest), uv),
        s_bloom = texture(sampler2D(c_bloom, smp_nearest), uv);

    // direct copy before modifying
    frag_color = s_color;
    frag_light = s_light;
    frag_bloom = s_bloom;

    const int index_type_flags = floatBitsToInt(s_pos_v_index_type_flags.w);
    const int buffer_index = index_type_flags >> 16;
    const int type_flags = index_type_flags & 0xFFFF;
    const int render_type = type_flags & RENDER_TYPE_MASK;

    if (render_type == RENDER_TYPE_MODEL) {
        const int model_flags = floatBitsToInt(s_extra_id_pos.x);
        
        if ((model_flags & MRF_ANY_HAND) != 0) {
            vec3 pcolor = compute_particles_color();
            pcolor = mix(pcolor, override_color.rgb, override_color.a);

            float intensity = rgb_to_hsv(pcolor).z;

            vec2 coord = uv;
            coord *= 1.3;
            float n = saturate(fbm(vec3(coord.x, coord.y - (time_s * 1.6), time_s * 0.3)));
            n = ease_in_out_quart(n);
            n = mix(0.7, 1.0, n);
            pcolor *= n;

            if (intensity > 0.001) {
                frag_color.rgb = mix(frag_color.rgb, pcolor, 0.3);

                frag_light += vec4(pcolor, saturate(intensity - 0.8));

                pcolor = vec3(1.0, 0.3, 0.3);
                frag_bloom += vec4(pcolor, saturate(intensity - 0.5));
            }
        }
    }
}

@end

@program program vs fs
