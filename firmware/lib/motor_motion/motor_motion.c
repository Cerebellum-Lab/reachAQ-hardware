#include "motor_motion.h"

#include <stddef.h>
#include <errno.h>
#include <fenv.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define CONFIG_USE_CORDIC 0

#pragma STDC FENV_ACCESS on

int init_context_struct(float displacement, float max_velocity, float max_acceleration, motor_context_t *context) {
  feclearexcept(FE_ALL_EXCEPT);

  context->a_max = max_acceleration;
  context->v_max = max_velocity;

  context->sgn = signbit(displacement) ? -1.0f : 1.0f;
  context->y_f = fabs(displacement);
  context->y_s = context->y_f / 2.0f;
  context->y_aux = max_velocity * max_velocity / max_acceleration;
  context->y_a = context->y_s < context->y_aux ? context->y_s : context->y_aux;

  context->v_w = context->y_s <= context->y_aux ? sqrt(context->y_s * max_acceleration) : max_velocity;

  context->t_o = context->v_w / max_acceleration;
  context->t_a = 2.0f * context->t_o;

  context->omega = 2.0f * M_PI / context->t_a;

  context->k_s = context->t_a * context->v_w / (4.0f * M_PI * M_PI);

  context->t_k = 2.0f * (context->y_s - context->y_a) / max_velocity;
  context->t_s = context->t_k / 2.0f + context->t_a;
  context->t_t = 2.0f * context->t_s;

  if (fetestexcept(FE_ALL_EXCEPT)) {
    feclearexcept(FE_ALL_EXCEPT);
    return -ERANGE;
  } else {
    return 0;
  }
}

static float motor_cos(float phi, motor_context_t *context) {
#if CONFIG_USE_CORDIC
  // Placeholder
#else
  (void) context;
  return cosf(phi);
#endif
}

static float displacement_hat(float time, motor_context_t *context) {
  if (time <= 0) {
    return 0;
  } else if (time <= context->t_a) {
    return context->a_max / 4.0f * time * time + context->k_s * (motor_cos(context->omega * time, context) - 1.0f);
  } else if (time <= context->t_s) {
    return context->y_s + context->v_w * (time - context->t_s);
  } else {
    // time > context->t_s
    return context->y_f - displacement_hat(context->t_t - time, context);
  }
}

static float displacement(float time, motor_context_t *context) {
  return context->sgn * displacement_hat(time, context);
}

int generate_displacement_table(float *table, size_t table_size, motor_context_t *context) {
  if (table_size == 0) {
    return 0;
  }

  float time_step = context->t_t / (table_size - 1);

  for (size_t i = 0; i < table_size; i++) {
    table[i] = displacement(time_step * i, context);
  }

  return 0;
}

