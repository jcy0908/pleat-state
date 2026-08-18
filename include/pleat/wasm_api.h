#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ps_create(int columns, int rows, float width, float height);
void ps_destroy(void);
void ps_reset(void);
void ps_step(float delta_seconds);
void ps_set_params(float stiffness,
                   float damping,
                   float gravity,
                   float wind,
                   float pleat_amplitude,
                   float pleat_frequency);
int ps_pointer_down(float x, float y, float radius);
void ps_pointer_move(float x, float y);
void ps_pointer_up(void);
uint32_t ps_particle_count(void);
const float* ps_positions_ptr(void);
uint32_t ps_index_count(void);
const uint32_t* ps_indices_ptr(void);
float ps_energy(void);

#ifdef __cplusplus
}
#endif
