@block common

@include ../shared_defs.h
@include ../config.h

// frag data unpacked from deferred textures
// see composite.glsl
struct composite_frag_data {
    vec3 color;
    vec3 pos_w, pos_w_fake, pos_v;
    vec3 n_w;
    vec2 sprite_size;
    vec2 uv;
    vec2 pos_dither;
    vec3 pos_quantized;
    int id;
    int buffer_index;
    int type_flags;

    vec3 extra_light;
    vec4 extra_bloom;

    // "pos" for frag_extra_id_pos, set by each function
    vec2 pos_ex;

    // "ex" for frag_extra_id_pos, set by each function
    // model: ignored
    // side: texture
    // sector: is_ceil
    // sprite: ignored
    // decal: is_ceil if sector, otherwise ignored
    float ex;

    // MRF_* for models
    int model_render_flags;

    // true if this frag is sky
    bool is_sky;
};

#define PI 3.14159265359
#define TAU (2.0 * PI)
#define PI_2 (PI / 2.0)
#define PI_4 (PI / 4.0)

// pack v4 into u32 (u8x4)
int vec4_to_u8x4(vec4 v) {
    return 0
        | ((int((v.a) * 255.0)) << 24)
        | ((int((v.b) * 255.0)) << 16)
        | ((int((v.g) * 255.0)) <<  8)
        | ((int((v.r) * 255.0)) <<  0);
}

// u32 (u8x4) -> v4
vec4 u8x4_to_vec4(int u) {
    return
        vec4(
            ((u >>  0) & 0xFF) / 255.0f,
            ((u >>  8) & 0xFF) / 255.0f,
            ((u >> 16) & 0xFF) / 255.0f,
            ((u >> 24) & 0xFF) / 255.0f);
}

// v4 -> u32 (u8x4) -> f32
float vec4_to_float(vec4 v) {
    return intBitsToFloat(vec4_to_u8x4(v));
}

// f32 -> u32 (u8x4) -> v4
vec4 float_to_vec4(float f) {
    return u8x4_to_vec4(floatBitsToInt(f));
}

// lerp but with unconstrained t
vec2 combine(vec2 a, vec2 b, float t) {
    const vec2 diff = b - a;
    return a + ((b - a) * t);
}

// lerp but with unconstrained t
vec3 combine(vec3 a, vec3 b, float t) {
    const vec3 diff = b - a;
    return a + ((b - a) * t);
}

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

float rand(vec3 co) {
    return fract(sin(dot(co, vec3(12.9898, 78.233, 59.4))) * 43758.5453);
}

// random in [0..1]^3
vec3 rand3(vec3 c) {
	float j = 4096.0 * sin(dot(c, vec3(17.0, 59.4, 15.0)));
	vec3 r;
	r.z = fract(512.0 * j);
	j *= 0.125;
	r.x = fract(512.0 * j);
	j *= 0.125;
	r.y = fract(512.0 * j);
	return r;
}

float hash( float n )
{
    return fract(sin(n)*43758.5453);
}

float noise(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);

    f = f*f*(3.0-2.0*f);

    float n = p.x + p.y*57.0 + 113.0*p.z;

    float res = mix(mix(mix( hash(n+  0.0), hash(n+  1.0),f.x),
                        mix( hash(n+ 57.0), hash(n+ 58.0),f.x),f.y),
                    mix(mix( hash(n+113.0), hash(n+114.0),f.x),
                        mix( hash(n+170.0), hash(n+171.0),f.x),f.y),f.z);
    return res;
}

float fbm(vec3 p) {
    mat3 m = mat3( 0.00,  0.80,  0.60,
              -0.80,  0.36, -0.48,
              -0.60, -0.48,  0.64 );
    float f;
    f  = 0.5000*noise( p ); p = m*p*2.02;
    f += 0.2500*noise( p ); p = m*p*2.03;
    f += 0.1250*noise( p ); p = m*p*2.01;
    f += 0.0625*noise( p );
    return f;
}

// 3D simplex noise (MIT!): https://www.shadertoy.com/view/XsX3zB

/* skew constants for 3d simplex functions */
const float F3 =  0.3333333;
const float G3 =  0.1666667;

/* 3d simplex noise (-1..1) */
float simplex_3d(vec3 p) {
	 /* 1. find current tetrahedron T and it's four vertices */
	 /* s, s+i1, s+i2, s+1.0 - absolute skewed (integer) coordinates of T vertices */
	 /* x, x1, x2, x3 - unskewed coordinates of p relative to each of T vertices*/

	 /* calculate s and x */
	 vec3 s = floor(p + dot(p, vec3(F3)));
	 vec3 x = p - s + dot(s, vec3(G3));

	 /* calculate i1 and i2 */
	 vec3 e = step(vec3(0.0), x - x.yzx);
	 vec3 i1 = e*(1.0 - e.zxy);
	 vec3 i2 = 1.0 - e.zxy*(1.0 - e);

	 /* x1, x2, x3 */
	 vec3 x1 = x - i1 + G3;
	 vec3 x2 = x - i2 + 2.0*G3;
	 vec3 x3 = x - 1.0 + 3.0*G3;

	 /* 2. find four surflets and store them in d */
	 vec4 w, d;

	 /* calculate surflet weights */
	 w.x = dot(x, x);
	 w.y = dot(x1, x1);
	 w.z = dot(x2, x2);
	 w.w = dot(x3, x3);

	 /* w fades from 0.6 at the center of the surflet to 0.0 at the margin */
	 w = max(0.6 - w, 0.0);

	 /* calculate surflet components */
	 d.x = dot(rand3(s) - 0.5, x);
	 d.y = dot(rand3(s + i1) - 0.5, x1);
	 d.z = dot(rand3(s + i2) - 0.5, x2);
	 d.w = dot(rand3(s + 1.0) - 0.5, x3);

	 /* multiply d by w^4 */
	 w *= w;
	 w *= w;
	 d *= w;

	 /* 3. return the sum of the four surflets */
	 return dot(d, vec4(52.0));
}

float saturate(float x) { return clamp(x, 0, 1); }
vec2 saturate(vec2 v) { return vec2(saturate(v.x), saturate(v.y)); }
vec3 saturate(vec3 v) { return vec3(saturate(v.x), saturate(v.y), saturate(v.z)); }

float plane_get_z(vec4 plane, vec2 point) {
    // solve ax + by + cz + d = 0 for z
    // z = (-d - ax - by) / c
    return (-plane.w - (plane.x * point.x) - (plane.y * point.y)) / plane.z;
}

vec3 plane_project(vec4 plane, vec3 point) {
    const vec3 n = plane.xyz, p = plane.xyz * -plane.w;
    return point - (dot(point - p, n) * n);
}

// >0 in front, <0 behind, =0 on
float plane_classify(vec4 plane, vec3 point) {
    return dot(plane, vec4(point, 1));
}

float ease_in_expo(float x) {
    return pow(2.0, 5.0 * x - 5.0);
}

float ease_in_out_quart(float x) {
    return x < 0.5 ? 8 * x * x * x * x : 1 - pow(-2 * x + 2, 4) / 2;
}

float cross(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x;
}

bool intersect_segs(
        vec2 a0, vec2 a1,
        vec2 b0, vec2 b1,
        out vec2 hit, out float t_out) {
    const vec2
        a10 = a0 - a1,
        b10 = b0 - b1,
        b0a0 = a0 - b0;

    const float
        d = 1.0 / cross(a10, b10),
        t = cross(b0a0, b10) * d,
        u = cross(b0a0, a10) * d;

    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) {
        hit = vec2(0);
        t_out = 0.0;
        return false;
    }

    hit = mix(a0, a1, t);
    t_out = t;
    return true;
}
// <0 right, 0 on, >0 left
float point_side(vec2 _p, vec2 _a, vec2 _b) {
    return
        -(((_p.x - _a.x) * (_b.y - _a.y))
            - ((_p.y - _a.y) * (_b.x - _a.x)));
}

vec2 point_project_segment(vec2 _p, vec2 _a, vec2 _b) {
    const float _l = length(_a - _b);
    if (_l < 0.000001f) { return _a; }

    const float _t =
        clamp(
            dot(_p - _a, _b - _a) / pow(_l, 2),
            0, 1);
    return mix(_a, _b, _t);
}

vec3 point_project_segment(vec3 _p, vec3 _a, vec3 _b) {
    const float _l = length(_a - _b);
    if (_l < 0.000001f) { return _a; }
    const float _t =
        clamp(
            dot(_p - _a, _b - _a) / pow(_l, 2),
            0, 1);
    return mix(_a, _b, _t);
}

vec2 point_project_line(vec2 p, vec2 a, vec2 b) {
    const vec2 d = b - a;
    const float l2 = dot(d, d);
    if (l2 < 0.000001f) { return a; }

    const float t = dot(p - a, b - a) / l2;
    return a + (t * d);
}

vec3 point_project_line(vec3 p, vec3 a, vec3 b) {
    const vec3 d = b - a;
    const float l2 = dot(d, d);
    if (l2 < 0.000001f) { return a; }

    const float t = dot(p - a, b - a) / l2;
    return a + (t * d);
}

vec2 project_vec(vec2 a, vec2 b) {
    return b * (dot(a, b) / dot(b, b));
}

float _atan2(in float y, in float x) {
    bool s = (abs(x) > abs(y));
    return mix(PI_2 - atan(x, y), atan(y, x), s);
}

float angle_wrap_tau(float a) {
    a = mod(a, TAU);
    return a < 0 ? (a + TAU) : a;
}

vec3 rgb_to_hsv(vec3 rgb) {
    float r = rgb.r, g = rgb.g, b = rgb.b;

    float K = 0.f;
    if (g < b)
    {
        // swap(g, b);
        float t = g;
        g = b;
        b = t;
        K = -1.f;
    }
    if (r < g)
    {
        // swap(r, g);
        float t = r;
        r = g;
        g = t;
        K = -2.f / 6.f - K;
    }

    const float chroma = r - (g < b ? g : b);

    vec3 hsv;
    hsv.x = abs(K + (g - b) / (6.f * chroma + 1e-20f));
    hsv.y = chroma / (r + 1e-20f);
    hsv.z = r;
    return hsv;
}

// Convert hsv floats ([0-1],[0-1],[0-1]) to rgb floats ([0-1],[0-1],[0-1]), from Foley & van Dam p593
// also http://en.wikipedia.org/wiki/HSL_and_HSV
vec3 hsv_to_rgb(vec3 hsv) {
    float h = hsv.x, s = hsv.y, v = hsv.z;
    if (s == 0.0f) {
        // gray
        return vec3(v);
    }

    h = mod(h, 1.0f) / (60.0f / 360.0f);
    int   i = int(h);
    float f = h - float(i);
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    float r, g, b;
    if (i == 0) { r = v; g = t; b = p; }
    else if (i == 1) { r = q; g = v; b = p; }
    else if (i == 2) { r = p; g = v; b = t; }
    else if (i == 3) { r = p; g = q; b = v; }
    else if (i == 4) { r = t; g = p; b = v; }
    else { r = v; g = p; b = q; }
    return vec3(r, g, b);
}

vec3 rgb_to_xyz(vec3 c) {
    vec3 tmp;
    tmp.x = (c.x > 0.04045) ? pow((c.x + 0.055) / 1.055, 2.4) : c.x / 12.92;
    tmp.y = (c.y > 0.04045) ? pow((c.y + 0.055) / 1.055, 2.4) : c.y / 12.92;
    tmp.z = (c.z > 0.04045) ? pow((c.z + 0.055) / 1.055, 2.4) : c.z / 12.92;

    mat3 mat;
    mat[0] = vec3(0.4124, 0.3576, 0.1805);
    mat[1] = vec3(0.2126, 0.7152, 0.0722);
    mat[2] = vec3(0.0193, 0.1192, 0.9505);
    return (mat * tmp) * 100.0f;
}

vec3 xyz_to_rgb(vec3 c) {
    mat3 mat;
    mat[0] = vec3(3.2406, -1.5372, -0.4986);
    mat[1] = vec3(-0.9689, 1.8758, 0.0415);
    mat[2] = vec3(0.0557, -0.2040, 1.0570);
    vec3 v = mat * (c * (1.0f / 100.0f));
    vec3 r;
    r.x = (v.x > 0.0031308) ? ((1.055 * pow(v.x, (1.0 / 2.4))) - 0.055) : 12.92 * v.x;
    r.y = (v.y > 0.0031308) ? ((1.055 * pow(v.y, (1.0 / 2.4))) - 0.055) : 12.92 * v.y;
    r.z = (v.z > 0.0031308) ? ((1.055 * pow(v.z, (1.0 / 2.4))) - 0.055) : 12.92 * v.z;
    return r;
}

// offsets some RGB color with HSV in [-1, 1]
vec3 offset_with_hsv(vec3 rgb, vec3 hsv) {
    vec3 col_hsv = rgb_to_hsv(rgb);
    col_hsv.x = abs(mod(col_hsv.x + hsv.x, 1.0f));
    col_hsv.y = clamp(col_hsv.y + hsv.y, 0, 1);
    col_hsv.z = clamp(col_hsv.z + hsv.z, 0, 1);
    return hsv_to_rgb(col_hsv);
}

// offsets some RGBA color with HSVA in [-1, 1]
vec4 offset_with_hsva(vec4 rgba, vec4 hsva) {
    return vec4(offset_with_hsv(rgba.rgb, hsva.rgb), clamp(rgba.a + hsva.a, 0.0, 1.0));
}

vec4 downsample(vec4 color) {
    // downsample to 4-bpp
    color.r = ((int(color.r * 255.0) / 9) * 9) / 255.0;
    color.g = ((int(color.g * 255.0) / 9) * 9) / 255.0;
    color.b = ((int(color.b * 255.0) / 9) * 9) / 255.0;
    return color;
}

vec4 gamma_adjust(vec4 color, float gamma) {
    gamma = mix(1.0, 2.2, gamma);
    color.rgb = clamp(pow(color.rgb, vec3(1.0 / gamma)), vec3(0), vec3(1));
    return color;
}

// https://aras-p.info/texts/CompactNormalStorage.html
vec2 normal_encode(vec3 n) {
    const float scale = 1.7777;
    vec2 enc = n.xy / (n.z+1);
    enc /= scale;
    enc = enc*0.5+0.5;
    return enc;
}

vec3 normal_decode(vec2 enc) {
    const float scale = 1.7777;
    vec3 nn =
        vec3(enc.xy, 0) * vec3(2*scale,2*scale,0) +
        vec3(-scale,-scale,1);
    const float g = 2.0 / dot(nn.xyz,nn.xyz);
    vec3 n;
    n.xy = g*nn.xy;
    n.z = g-1;
    return n;
}

// https://www.gamedev.net/forums/topic/687535-implementing-a-cube-map-lookup-function/5337472/
vec2 sample_cube(
        const vec3 v,
        out int face_index) {
	vec3 v_abs = abs(v);
	float ma;
	vec2 uv;
	if(v_abs.z >= v_abs.x && v_abs.z >= v_abs.y) {
		face_index = v.z < 0.0 ? 5 : 4;
		ma = 0.5 / v_abs.z;
		uv = vec2(v.z < 0.0 ? -v.x : v.x, -v.y);
	} else if(v_abs.y >= v_abs.x) {
		face_index = v.y < 0.0 ? 3 : 2;
		ma = 0.5 / v_abs.y;
		uv = vec2(v.x, v.y < 0.0 ? -v.z : v.z);
	} else {
		face_index = v.x < 0.0 ? 1 : 0;
		ma = 0.5 / v_abs.x;
		uv = vec2(v.x < 0.0 ? v.z : -v.z, -v.y);
	}

    uv = clamp(uv * ma + 0.5, vec2(0), vec2(1));
    //uv.t = 1.0 - uv.t;
    uv.s = 1.0 - uv.s;
    return uv;
}

vec2 uv_rotate(vec2 uv, float rotation) {
    if (abs(rotation) < 0.00001) { return uv; }

    vec2 uv_r = uv;
    const float
        co = cos(rotation),
        si = sin(rotation);
    uv_r -= 0.5f;
    uv_r *= mat2(co, -si, si, co);
    uv_r += 0.5f;
    uv_r = clamp(uv_r, vec2(0.0), vec2(1.0));
    return uv_r;
}

vec2 rotate_vec2(vec2 v, float angle) {
    const float s = sin(angle), c = cos(angle);
    return vec2((v.x * c) - (v.y * s), (v.x * s) + (v.y * c));
}

vec2 left_side_normal(vec2 a, vec2 b) {
    return normalize(vec2(-(b.y - a.y), b.x - a.x));
}

mat4 mat4_identity() {
    return mat4(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}

mat4 mat4_translate_make(vec3 d) {
    return mat4(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        d.x, d.y, d.z, 1
    );
}

mat4 mat4_scale_make(vec3 s) {
    return mat4(
        s.x, 0,   0,   0,
        0,   s.y, 0,   0,
        0,   0,   s.z, 0,
        0,   0,   0,   1
    );
}

// https://github.com/dmnsgn/glsl-rotate/blob/main/rotation-3d.glsl
mat4 mat4_rotate_make(float a, vec3 axis) {
    vec3 axisn, v, vs;
    float c;

    c = cos(a);

    axisn = normalize(axis);
    v = axisn * (1.0 - c);
    vs = axisn * sin(a);
    // glm_vec3_scale(axisn, 1.0 - c, v);
    // glm_vec3_scale(axisn, sinf(angle), vs);


    mat4 m;

    m[0].xyz = axisn * v[0]; // glm_vec3_scale(axisn, v[0], m[0]);
    m[1].xyz = axisn * v[1]; // glm_vec3_scale(axisn, v[1], m[1]);
    m[2].xyz = axisn * v[2]; // glm_vec3_scale(axisn, v[2], m[2]);

    m[0][0] += c;       m[1][0] -= vs[2];   m[2][0] += vs[1];
    m[0][1] += vs[2];   m[1][1] += c;       m[2][1] -= vs[0];
    m[0][2] -= vs[1];   m[1][2] += vs[0];   m[2][2] += c;

    m[0][3] = m[1][3] = m[2][3] = m[3][0] = m[3][1] = m[3][2] = 0.0f;
    m[3][3] = 1.0f;
    return m;
}

// "replace" v with w, having other axes follow suit
mat4 mat4_rotate_make_from_to(vec3 v, vec3 w) {
    v = normalize(v);
    w = normalize(w);

    const float d = dot(v, w);
    vec3 axis = normalize(cross(v, w));
    float angle = acos(d);

    if (d > 0.9999f) {
        // parallel
        return mat4_identity();
    } else if (d < -0.9999f) {
        // anti-parallel
        axis = cross(v, vec3(0, 1, 0));
        if (dot(axis, axis) < 0.0001f) {
            axis = cross(v, vec3(1, 0, 0));
        }
        angle = PI;
    }

    return mat4_rotate_make(angle, axis);
}

vec3 pixel_to_near_plane_world_space(
        mat4 inv_view_proj,
        vec2 px,
        ivec2 size) {
    vec2 p_ndc = ((px / vec2(size)) * 2) - 1;
    // TODO(opengl): maybe not necessary
    p_ndc.y = -p_ndc.y;
    // TODO(opengl): maybe -1 for near plane NDC
    vec4 p = inv_view_proj * vec4(p_ndc, 0, 1);
    p.xyz /= p.w;
    return p.xyz;
}

vec3 pixel_to_camera_dir(mat4 inv_view_proj, vec3 cam_pos, vec2 px, ivec2 size) {
    const vec3 pos_w = pixel_to_near_plane_world_space(inv_view_proj, px, size);
    return normalize(pos_w - cam_pos);
}

// 0 = 0%, 1 = 100%, 1.5 = 150%, etc.
vec3 con_sat_brt(vec3 col, float con, float sat, float brt) {
    const vec3 l_coeff = vec3(0.2125, 0.7154, 0.0721);
    const vec3 l_avg = vec3(0.5);
    const vec3 col_brt = col * brt;
    const vec3 intensity = vec3(dot(col_brt, l_coeff));
    const vec3 col_sat = mix(intensity, col_brt, sat);
    const vec3 col_con = mix(l_avg, col_sat, con);
    return col_con;
}

@end

@block tex_atlas
readonly buffer tex_atlas_entries_buffer {
    tex_atlas_entry_info_t tex_atlas_entries[];
};

uniform texture2DArray tex_atlas;
uniform texture2D tex_atlas_virtual;

struct tex_atlas_entry {
    vec2 mi, ma, size;
    int layer;
};

void tex_atlas_lookup(int texture_id, int tick, out tex_atlas_entry entry) {
    tex_atlas_entry_info_t info = tex_atlas_entries[texture_id];
    entry.mi = info.uv_min;
    entry.ma = info.uv_max;
    entry.size = entry.ma - entry.mi;
    entry.layer = info.layer;

    const int anim_ticks = info.anim_ticks_per_frame;
    if (anim_ticks > 0) {
        const int anim_width = info.anim_width;

        const float frame_width_uv = anim_width / float(TEX_ATLAS_SIZE);
        const int
            n_frames = int(entry.size.x / frame_width_uv),
            frame = (tick / 10) % n_frames;

        // scale entry x size down
        entry.size.x = frame_width_uv;
        entry.mi.x += frame_width_uv * (frame + 0);
        entry.ma.x += frame_width_uv * (frame + 1);
    }
}

vec4 tex_atlas_sample(vec3 uvl) {
    if (int(uvl.z) == TEX_ATLAS_LAYER_VIRTUAL) {
        return texture(sampler2D(tex_atlas_virtual, smp_nearest), uvl.xy);
    } else {
        // TODO: do layers need to be hardcoded?
        return texture(sampler2DArray(tex_atlas, smp_nearest), vec3(uvl.xy, 1.0));
    }
}

@end

@block level_buffers

readonly buffer side_render_data_buffer {
    side_render_data_t side_render_data[];
};

readonly buffer sector_render_data_buffer {
    sector_render_data_t sector_render_data[];
};

readonly buffer decal_render_data_buffer {
    decal_render_data_t decal_render_data[];
};

readonly buffer model_render_data_buffer {
    model_render_data_t model_render_data[];
};

readonly buffer sprite_render_data_buffer {
    sprite_render_data_t sprite_render_data[];
};

@end
