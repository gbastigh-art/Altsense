@module batch

@include ctypes.glsl
@include common.glsl

@vs vs
@include_block common

in vec2 a_position;
in vec2 a_texcoord0;

in vec2 a_offset;
in vec2 a_scale;
in vec2 a_uv_min;
in vec2 a_uv_max;
in vec4 a_color;
in float a_z;
in float a_flags;
in float a_tex;

uniform vs_params {
	mat4 model;
    mat4 view;
    mat4 proj;
};

flat out vec2 uv_min;
flat out vec2 uv_max;
flat out int tex;
flat out int flags;

out vec2 uv;
out vec4 color;

void main() {
    uv = a_texcoord0;

    if ((flags & GFX_FLIP_X) != 0) {
        uv.x = 1.0 - uv.x;
    }

    if ((flags & GFX_FLIP_Y) != 0) {
        uv.y = 1.0 - uv.y;
    }

    uv_min = a_uv_min;
    uv_max = a_uv_max;
    tex = floatBitsToInt(a_tex);
    flags = floatBitsToInt(a_flags);
    color = a_color;

    const vec2 ipos = a_offset + (a_scale * a_position);
    gl_Position = proj * view * model * vec4(ipos, a_z, 1.0);
}
@end

@fs fs

uniform sampler smp_nearest;

@include_block common
@include_block tex_atlas

uniform texture3D palette;

uniform fs_params {
    int tick;
};

flat in vec2 uv_min;
flat in vec2 uv_max;
flat in int tex;
flat in int flags;

in vec2 uv;
in vec4 color;

out vec4 frag_color;

void main() {
    // TODO: doesn't work with anything animated? check if animated maybe
    tex_atlas_entry entry;
    tex_atlas_lookup(tex, tick, entry);

    const vec3 coords = vec3(mix(uv_min, uv_max, uv), entry.layer);
    frag_color = tex_atlas_sample(coords);
    frag_color *= color;
    // frag_color += vec4(1);

    // if ((flags & GFX_DITHER) != 0) {
    //     const int DITHER_MATRIX[64] = {
    //          0, 32, 8,  40, 2,  34, 10, 42,
    //         48, 16, 56, 24, 50, 18, 58, 26,
    //         12, 44, 4,  36, 14, 46, 6,  38,
    //         60, 28, 52, 20, 62, 30, 54, 22,
    //          3, 35, 11, 43, 1,  33, 9,  41,
    //         51, 19, 59, 27, 49, 17, 57, 25,
    //         15, 47, 7,  39, 13, 45, 5,  37,
    //         63, 31, 55, 23, 61, 29, 53, 21
    //     };

    //     const ivec2 size_px = ivec2((uv_max - uv_min) / TEX_ATLAS_UNIT);

    //     ivec2 pos_dither = ivec2(texcoord * size_px);
    //     if ((flags & GFX_DITHER_INTENSE) != 0) {
    //         pos_dither += ivec2(rand(pos_dither) * ivec2(30));
    //         pos_dither += ivec2(((tick + 0) / 5) % 30, ((tick + 20) / 5) % 30);
    //         const int q =
    //             ((pos_dither.x * 17 % 8) * 8) + ((pos_dither.y * 31) % 8);
    //         const float dither_offset =
    //             mix(
    //                 ((DITHER_MATRIX[q] - 0.5) / 255.0) / 0.25,
    //                 rand(pos_dither),
    //                 0.5) * 0.5;

    //         frag_color.rgb =
    //             length(frag_color.rgb + dither_offset) > 0.65 ?
    //                 vec3(1) : vec3(0);
    //     } else {
    //         pos_dither += ivec2(((tick + 0) / 10) % 3, ((tick + 20) / 10) % 3);

    //         const float r = 1.0 / 4.0;
    //         const int q =
    //             ((pos_dither.x * 17 % 8) * 8) + ((pos_dither.y * 31) % 8);
    //         const float dither_offset = r * ((DITHER_MATRIX[q] - 0.5) / 255.);

    //         frag_color.rgb =
    //             texture(
    //                 sampler3D(palette, smp),
    //                 frag_color.rgb + dither_offset).rgb;
    //     }
    // }

    if (frag_color.a < 0.0001) {
        discard;
    }
}
@end

@program program vs fs
