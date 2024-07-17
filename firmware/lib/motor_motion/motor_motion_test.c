#include "motor_motion.h"

#include <stddef.h>
#include <stdio.h>

int print_table() {
    motor_context_t context;
    static float displacement_table[500];
    init_context_struct(400.0, 60.0 / 0.14, 2000.0, &context);
    printf("a_max: %f\n", context.a_max);
    printf("v_max: %f\n", context.v_max);
    printf("sgn: %f\n", context.sgn);
    printf("y_f: %f\n", context.y_f);
    printf("y_s: %f\n", context.y_s);
    printf("y_aux: %f\n", context.y_aux);
    printf("y_a: %f\n", context.y_a);
    printf("v_w: %f\n", context.v_w);
    printf("t_o: %f\n", context.t_o);
    printf("t_a: %f\n", context.t_a);
    printf("omega: %f\n", context.omega);
    printf("k_s: %f\n", context.k_s);
    printf("t_k: %f\n", context.t_k);
    printf("t_s: %f\n", context.t_s);
    printf("t_t: %f\n", context.t_t);

    int ret = generate_displacement_table(displacement_table, 500, &context);

    for (size_t i = 0; i < 500; i++)
    {
        printf("%0.16f,\n", displacement_table[i]);
    }
    
    return ret;
}

int main() {
    return print_table();
}