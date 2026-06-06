#ifndef FX_DETUNE_H
#define FX_DETUNE_H

void fx_detune_init(void);
void fx_detune_set_base(int v, float hz);
void fx_detune_update(float temp_c, float dt_s);
float fx_detune_get_cents(void);

#endif
