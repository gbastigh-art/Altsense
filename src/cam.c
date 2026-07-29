#include "cam.h"
#include "game.h"

v3 cam_dir_right_up(f32 cd, f32 cr, f32 cu) {
    return
        v3_add(
            v3_scale(g->cam.dir, cd),
            v3_add(
                v3_scale(g->cam.right, cr),
                v3_scale(g->cam.up, cu)));
}

v2 world_pos_to_clamped_pixel(
        const m4 *view_proj,
        const frustum_3d_t *frustum,
        v2i size,
        v3 pos) {
    // clamp to view frustum
    for (int i = 0; i < 6; i++) {
        if (plane_classify(frustum->planes[i], pos) < 0) {
            pos = plane_project(frustum->planes[i], pos);
        }
    }

    v4 p_clip = m4_mulv(*view_proj, v4_of(pos, 1.0f));
    p_clip.w = ifnaninf(p_clip.w, 1.0f, 1.0f);

    const v3 p_ndc = v3_divs(v3_from(p_clip), p_clip.w);
    const v2 p_px =
        v2_mul(
            v2_of(
                0.5f * (p_ndc.x + 1.0f),
                0.5f * (p_ndc.y + 1.0f)),
            v2_of(size.x - 1, size.y - 1));
    v2 pos_2d = p_px;
    pos_2d =
        v2_clampv(
            pos_2d,
            v2_of(0.0f),
            v2_of(size.x - 1, size.y - 1));
    return pos_2d;
}

bool world_pos_to_pixel(
        const m4 *view_proj,
        const frustum_3d_t *frustum,
        v2i size,
        v3 pos,
        v2 *out) {
    // clamp to view frustum
    for (int i = 0; i < 6; i++) {
        if (plane_classify(frustum->planes[i], pos) < 0) {
            pos = plane_project(frustum->planes[i], pos);
        }
    }

    v4 p_clip = m4_mulv(*view_proj, v4_of(pos, 1.0f));

    if (p_clip.x < -p_clip.w || p_clip.x >= +p_clip.w
        || p_clip.y < -p_clip.w || p_clip.y >= +p_clip.w
        || p_clip.z < 0.0f || p_clip.z >= +p_clip.w) {
        return false;
    }

    p_clip.w = ifnaninf(p_clip.w, 1.0f, 1.0f);

    const v3 p_ndc = v3_divs(v3_from(p_clip), p_clip.w);
    *out =
        v2_mul(
            v2_of(
                0.5f * (p_ndc.x + 1.0f),
                0.5f * (p_ndc.y + 1.0f)),
            v2_of(size.x - 1, size.y - 1));
    return true;
}

void cam_near_plane_world_space(const m4 *inv_view_proj, v3 *points) {
    points[0] = v3_from(m4_mulv(*inv_view_proj, v4_of(-1.0f, -1.0f, 0.0f, 1.0f)));
    points[1] = v3_from(m4_mulv(*inv_view_proj, v4_of(-1.0f, +1.0f, 0.0f, 1.0f)));
    points[2] = v3_from(m4_mulv(*inv_view_proj, v4_of(+1.0f, +1.0f, 0.0f, 1.0f)));
    points[3] = v3_from(m4_mulv(*inv_view_proj, v4_of(+1.0f, -1.0f, 0.0f, 1.0f)));
}

v3 cam_pixel_to_near_plane_world_space(
        const m4 *inv_view_proj,
        v2 pixel,
        v2i size) {
    const v2 p_ndc =
        v2_adds(v2_scale(v2_div(pixel, v2_from_i(size)), 2.0f), -1.0f);
    v4 p = m4_mulv(*inv_view_proj, v4_of(p_ndc, 0.0f, 1.0f));
    p.x /= p.w;
    p.y /= p.w;
    p.z /= p.w;
    return v3_from(p);
}

ray3f_t cam_ray_from_pixel(const m4 *inv_view_proj, v3 pos, v2 pixel, v2i size) {
    ray3f_t res;
    res.origin = pos;
    res.dir =
        v3_dir(
            pos,
            cam_pixel_to_near_plane_world_space(inv_view_proj, pixel, size));
    return res;
}
