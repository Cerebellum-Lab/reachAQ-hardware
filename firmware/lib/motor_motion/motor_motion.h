#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct motor_context {
  // Parameters from the paper
  float a_max;
  float v_max;
  float sgn;
  float y_f;
  float y_s;
  float y_aux;
  float y_a;
  float v_w;
  float t_o;
  float t_a;
  float omega;
  float k_s;
  float t_k;
  float t_s;
  float t_t;
} motor_context_t;

int init_context_struct(float displacement, float max_velocity, float max_acceleration, motor_context_t *context);
int generate_displacement_table(float *table, size_t table_size, motor_context_t *context);