#include "../lib/soloud/include/soloud.h"
#include "sound/collider.h"

struct CustomCollider : SoLoud::AudioCollider {
    audio_collide_f collide_fn;
    void *userdata;

    virtual ~CustomCollider() = default;

    float collide(SoLoud::Soloud *aSoloud, SoLoud::AudioSourceInstance3dData *d, int aUserData) override {
        return collide_fn(
            d->mHandle,
            d->m3dPosition[0], d->m3dPosition[1], d->m3dPosition[2],
            d->m3dVelocity[0], d->m3dVelocity[1], d->m3dVelocity[2],
            aUserData, userdata);
	}
};

extern "C" {
void *Collider_create(audio_collide_f fn, void *userdata, void *fp) {
    CustomCollider *cc = new CustomCollider();
    cc->collide_fn = fn;
    cc->userdata = userdata;
    if (fp) { *(void**)fp = &cc->collide_fn; }
    return cc;
}

void Collider_destroy(void *cc) {
    delete ((CustomCollider*) cc);
}
}
