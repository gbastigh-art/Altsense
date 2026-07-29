#pragma once

#ifdef __cplusplus
extern "C" {
#endif
typedef float (*audio_collide_f)(
    int handle, float pos_x, float pos_y, float pos_z, float v_x, float v_y, float v_z, int i_userdata, void *userdata);

void *Collider_create(audio_collide_f fn, void *userdata, void *fp);
void Collider_destroy(void *cc);
#ifdef __cplusplus
}
#endif
