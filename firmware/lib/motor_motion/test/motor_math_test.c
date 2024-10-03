/*
 * Generate servo and stepper values for a given motion profile. The profile is generated
 */

#include "motor_math.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void print_context_variables(const motor_motion_context_t *context) {
    fprintf(stderr, "Servo context:\n");
    fprintf(stderr, "start_pos: %f\n", context->start_pos);
    fprintf(stderr, "end_pos: %f\n", context->end_pos);
    fprintf(stderr, "a_max: %f\n", context->a_max);
    fprintf(stderr, "v_max: %f\n", context->v_max);
    fprintf(stderr, "sgn: %f\n", context->sgn);
    fprintf(stderr, "y_f: %f\n", context->y_f);
    fprintf(stderr, "y_s: %f\n", context->y_s);
    fprintf(stderr, "y_aux: %f\n", context->y_aux);
    fprintf(stderr, "y_a: %f\n", context->y_a);
    fprintf(stderr, "v_w: %f\n", context->v_w);
    fprintf(stderr, "t_o: %f\n", context->t_o);
    fprintf(stderr, "t_a: %f\n", context->t_a);
    fprintf(stderr, "omega: %f\n", context->omega);
    fprintf(stderr, "k_s: %f\n", context->k_s);
    fprintf(stderr, "t_k: %f\n", context->t_k);
    fprintf(stderr, "t_s: %f\n", context->t_s);
    fprintf(stderr, "t_t: %f\n", context->t_t);
}

#define SERVO_VALUES_NUM 1024U
static uint32_t servo_values[SERVO_VALUES_NUM];
void gen_servo_values(motor_motion_context_t *context) {
    ssize_t ret = motor_motion_servo_generate_displacement_table(servo_values, SERVO_VALUES_NUM, context);
    if (ret < 0) {
        fprintf(stderr, "Failed to generate servo values: %ld\n", ret);
    }
}

void print_servo_values(motor_motion_context_t *context) {
    for (size_t i = 0; i < SERVO_VALUES_NUM; i++) {
        printf("%u, %f\n", servo_values[i], 0.02 * i);
    }
}

#define STEPPER_VALUES_NUM 1024U
static uint32_t stepper_values[STEPPER_VALUES_NUM];
void gen_stepper_values(motor_motion_context_t *context) {
    ssize_t ret = motor_motion_stepper_generate_timing_table(stepper_values, STEPPER_VALUES_NUM, context);
    if (ret < 0) {
        fprintf(stderr, "Failed to generate stepper values: %ld\n", ret);
    }
}

void print_stepper_values(motor_motion_context_t *context) {
    float current_time = 0.0f;
    for (size_t i = 0; i < STEPPER_VALUES_NUM; i++) {
        printf("%f, %f\n", i * 0.5, current_time);
        current_time += stepper_values[i] / 200e3f;
    }
}

int main(int argc, char *argv[]) {
    motor_motion_context_t context;
    return 0;
}