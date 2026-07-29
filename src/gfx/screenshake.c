#include "gfx/screenshake.h"
#include "gfx/renderer.h"
#include "util/time.h"
#include "game.h"

void screenshake_add(const screenshake_t *desc) {
    screenshake_t s = *desc;
    s.start_ns = g->time.total_scaled_ns;
    if (s.rate == 0.0f) { s.rate = 1.0f; }

    if (!v3_eqv_eps(s.pos, v3_of(0))) {
        if (v3_norm(v3_sub(s.pos, g->cam.pos)) > desc->dist) {
            return;
        }

        s.is_3d = true;
    }

    *dynlist_push(g_renderer->screenshakes) = s;
}

v3 screenshake_sample_all(v3 cam_pos) {
    v3 d = v3_of(0);
    dynlist_each(g_renderer->screenshakes, it) {
        screenshake_t *sh = it.el;

        const f32 since = ns_to_secs(g->time.total_scaled_ns - sh->start_ns);
        if (since > sh->duration) {
            dynlist_remove_it(g_renderer->screenshakes, it);
            continue;
        }

        f32 effect = glm_ease_circ_in(1.0f - (since / sh->duration));

        const int divisor = (int) (20.0f * sh->rate);

        const int seed =
            (sh->start_ns % 1000)
                + (sh->strength * 17)
                + ((int) (divisor * since));

        rand_t rand = rand_create(seed);

        v3 target = rand_v3_dir(&rand);
        if (!v3_eqv_eps(sh->amplitude, v3_of(0))) {
            target = v3_normalize(v3_mul(target, sh->amplitude));
        }

        sh->d = v3_slerp(sh->d, target, 0.1f);

        if (sh->is_3d) {
            effect *=
                1.0 - satf(v3_norm(v3_sub(cam_pos, sh->pos)) / sh->dist);
        }

        d = v3_add(d, v3_scale(sh->d, effect * sh->strength * 0.04f));
    }
    return d;
}

void screenshake_clear() {
    dynlist_resize(g_renderer->screenshakes, 0);
}

