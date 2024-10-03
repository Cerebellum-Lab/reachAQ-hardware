#include "motor_math.h"

#include <argp.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int print_n_iterations = 0;

static float pulse_width_to_degrees(const servo_motor_context_t *context, const uint32_t pulse_width) {
    return ((float)pulse_width * context->pwm_timer_increment - context->min_angle_pwm) *
               (context->max_angle - context->min_angle) / (context->max_angle_pwm - context->min_angle_pwm) +
           context->min_angle;
}

static void print_context_variables(const motor_motion_profile_t *context) {
    fprintf(stderr, "Servo context:\n");
    fprintf(stderr, "start_pos: %f\n", context->start_pos);
    fprintf(stderr, "end_pos: %f\n", context->end_pos);
    fprintf(stderr, "a_max: %f\n", context->a_max);
    fprintf(stderr, "v_max: %f\n", context->v_max);
    fprintf(stderr, "sgn: %f\n", context->sgn);
    fprintf(stderr, "y_f: %f\n", context->y_f);
    fprintf(stderr, "y_s: %f\n", context->y_s);
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

static void print_servo_values(const uint32_t *servo_values, const size_t n_values) {
    for (size_t i = 0; i < n_values; i++) {
        printf("%u, %f\n", servo_values[i], 0.02 * i);
    }
}

static void print_stepper_values(stepper_motor_context_t *context, const uint32_t *stepper_values,
                                 const size_t n_values) {
    float current_time = 0.0f;
    for (size_t i = 0; i < n_values; i++) {
        printf("%f, %f\n", i * 0.5, current_time);
        current_time += (float)stepper_values[i] * context->timer_increment;
    }
}

static void servo_verify_max_velocity_and_acceleration(const servo_motor_context_t *context, const uint32_t *values,
                                                       const size_t n_values, const bool verify_velocity,
                                                       const bool verify_acceleration) {
    if (!verify_velocity && !verify_acceleration) {
        return;
    }

    float last_position = pulse_width_to_degrees(context, values[0]);
    float last_velocity = 0.0f;

    for (size_t i = 1; i < n_values; i++) {
        const float this_position = pulse_width_to_degrees(context, values[i]);
        const float velocity = (values[i] - last_position) / 0.02f;
        if (verify_velocity && fabsf(velocity) > context->motion_profile.v_max) {
            fprintf(stderr, "Average velocity at generated position %lu, is greater than max velocity: %f > %f\n", i,
                    velocity, context->motion_profile.v_max);
        }
        if (i > 1 && verify_acceleration) {
            const float acceleration = (velocity - last_velocity) / 0.02f;
            if (fabsf(acceleration) > context->motion_profile.a_max) {
                fprintf(stderr,
                        "Average acceleration at generated position %lu, is greater than max acceleration: %f > %f\n",
                        i, acceleration, context->motion_profile.a_max);
            }
        }
        last_position = this_position;
        last_velocity = velocity;
    }
}

static void stepper_verify_max_velocity_and_acceleration(const stepper_motor_context_t *context, const uint32_t *values,
                                                         const size_t n_values, const bool verify_velocity,
                                                         const bool verify_acceleration) {
    if (!verify_velocity && !verify_acceleration) {
        return;
    }

    float last_time = 0.0f;
    float last_velocity = 0.0f;
    for (size_t i = 0; i < n_values; i++) {
        const float this_time = last_time + (float)values[i] * context->timer_increment;
        const float velocity = context->min_step / (this_time - last_time);
        if (verify_velocity && fabsf(velocity) > context->motion_profile.v_max) {
            fprintf(stderr, "Average velocity at generated position %lu, is greater than max velocity: %f > %f\n", i,
                    velocity, context->motion_profile.v_max);
        }
        if (i > 0 && verify_acceleration) {
            const float acceleration = (velocity - last_velocity) / (this_time - last_time);
            if (fabsf(acceleration) > context->motion_profile.a_max) {
                fprintf(stderr,
                        "Average acceleration at generated position %lu, is greater than max acceleration: %f > %f\n",
                        i, acceleration, context->motion_profile.a_max);
            }
        }
        last_velocity = velocity;
        last_time = this_time;
    }
}

const char *argp_program_version = "motor_math_test .1";
const char *argp_program_bug_address = "";
const char doc[] = "Test the motor math library by building and using the CMake project in this directory.";
const char args_doc[] = "";

static struct argp_option options[] = {
    {"print", 'p', 0, 0, "Print entire model as csv to stdout.", 1},
    {"print-iterations", 'N', 0, 0, "Print the number of iterations of Halley's method each time.", 1},
    {"servo", 's', 0, 0, "Generate servo values.", 1},
    {"stepper", 't', 0, 0, "Generate stepper values.", 1},
    {0, 0, 0, 0, "General Parameters", 2},
    {"start", 'x', "start", 0, "Start position", 2},
    {"end", 'y', "end", 0, "End Position", 2},
    {"max-velocity", 'v', "velocity", 0, "Max velocity", 2},
    {"max-acceleration", 'a', "acceleration", 0, "Max acceleration", 2},
    {"radius", 'r', "radius", 0,
     "Radius of the rotor. If given, all units must be in terms of this, not steps or degrees.", 2},
    {0, 0, 0, 0, "Servo Parameters", 3},
    {"min-angle", 'm', "min", 0, "Minimum angle", 3},
    {"max-angle", 'M', "max", 0, "Maximum angle", 3},
    {"min-angle-pwm", 'u', "min_angle_pwm", 0, "PWM Duration (in us) of minimum angle", 3},
    {"max-angle-pwm", 'o', "max_angle_pwm", 0, "PWM Duration (in us) of maximum angle", 3},
    {0, 0, 0, 0, "Stepper Parameters", 4},
    {"min-step", 'n', "min-step", 0, "Minimum step", 4},
    {"timer-increment", 'i', "increment", 0, "Timer increment", 4},
    {"steps-per-revolution", 'S', "steps", 0, "Steps per revolution", 4},
    {0, 0, 0, 0, "Tests", 5},
    {"verify-max-velocity", 'V', 0, 0, "Verify max velocity", 5},
    {"verify-max-acceleration", 'A', 0, 0, "Verify max acceleration", 5},
    {0},
    {},
};

struct arguments {
    int print;
    int servo;
    int stepper;
    float start;
    float end;
    float max_velocity;
    float max_acceleration;
    float radius;
    float min_angle;
    float max_angle;
    float angle_adjustment;
    float servo_pwm_min_angle;
    float servo_pwm_max_angle;
    float min_step;
    float timer_increment;
    float steps_per_revolution;
    int verify_max_velocity;
    int verify_max_acceleration;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    char *end = NULL;
    struct arguments *arguments = state->input;
    switch (key) {
        case 'p':
            arguments->print = 1;
            break;
        case 'N':
            print_n_iterations = 1;
            break;
        case 's':
            arguments->servo = 1;
            break;
        case 't':
            arguments->stepper = 1;
            break;
        case 'x':
            arguments->start = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse start position: %s\n", arg);
            }
            break;
        case 'y':
            arguments->end = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse end position: %s\n", arg);
            }
            break;
        case 'v':
            arguments->max_velocity = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse max velocity: %s\n", arg);
            }
            break;
        case 'a':
            arguments->max_acceleration = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse max acceleartion: %s\n", arg);
            }
            break;
        case 'r':
            arguments->radius = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse radius: %s\n", arg);
            }
            break;
        case 'm':
            arguments->min_angle = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse min angle: %s\n", arg);
            }
            break;
        case 'M':
            arguments->max_angle = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse max angle: %s\n", arg);
            }
            break;
        case 'u':
            arguments->servo_pwm_min_angle = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse servo pwm min angle: %s\n", arg);
            }
            break;
        case 'o':
            arguments->servo_pwm_max_angle = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse servo pwm max angle: %s\n", arg);
            }
            break;
        case 'n':
            arguments->min_step = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse min stepn: %s\n", arg);
            }
            break;
        case 'i':
            arguments->timer_increment = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse timer increment: %s\n", arg);
            }
            break;
        case 'S':
            arguments->steps_per_revolution = strtof(arg, &end);
            if (*end != '\0') {
                fprintf(stderr, "Couldn't parse steps per revolution: %s\n", arg);
            }
            break;
        case 'V':
            arguments->verify_max_velocity = 1;
            break;
        case 'A':
            arguments->verify_max_acceleration = 1;
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

#define SERVO_VALUES_NUM 256U
static int model_and_verify_servo(const struct arguments *arguments) {
    static servo_motor_context_t servo_context = {0};
    static uint32_t servo_values[SERVO_VALUES_NUM];

    servo_context.motion_profile.radius = arguments->radius;

    if (arguments->max_velocity == 0.0f || arguments->max_acceleration == 0.0f) {
        fprintf(stderr, "Max velocity and max acceleration must be non-zero.\n");
        return -EINVAL;
    }

    if (arguments->start == arguments->end) {
        fprintf(stderr, "start and end position are the same.\n");
    }

    int ret = motor_motion_servo_init_context_struct(arguments->start, arguments->end, arguments->max_velocity,
                                                     arguments->max_acceleration, arguments->servo_pwm_min_angle,
                                                     arguments->servo_pwm_max_angle, &servo_context);
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize servo context: %d\n", ret);
        print_context_variables(&servo_context.motion_profile);
        return ret;
    }

    ssize_t n_vals = motor_motion_servo_generate_displacement_table(servo_values, SERVO_VALUES_NUM, &servo_context);
    ssize_t j = 0;
    while (n_vals > 0) {
        fprintf(stderr, "Run %ld of size %ld\n", j++, n_vals);
        if (arguments->verify_max_velocity || arguments->verify_max_acceleration) {
            servo_verify_max_velocity_and_acceleration(
                &servo_context, servo_values, ret, arguments->verify_max_velocity, arguments->verify_max_acceleration);
        }
        if (arguments->print) {
            print_servo_values(servo_values, ret);
        }
        n_vals = motor_motion_servo_generate_displacement_table(servo_values, SERVO_VALUES_NUM, &servo_context);
    }

    if (n_vals < 0) {
        fprintf(stderr, "Failed to generate servo values: %ld\n", n_vals);
        return -1;
    }

    return 0;
}

#define STEPPER_VALUES_NUM 1024U
static int model_and_verify_stepper(const struct arguments *arguments) {
    static stepper_motor_context_t stepper_context = {0};
    static uint32_t stepper_values[STEPPER_VALUES_NUM];

    stepper_context.steps_per_revolution = arguments->steps_per_revolution;
    stepper_context.motion_profile.radius = arguments->radius;

    if (arguments->max_velocity == 0.0f || arguments->max_acceleration == 0.0f) {
        fprintf(stderr, "Max velocity and max acceleration must be non-zero.\n");
        return -EINVAL;
    }

    if (arguments->min_step == 0) {
        fprintf(stderr, "Servo min_step is unset. Initializing to 1.\n");
    }

    if (arguments->steps_per_revolution == 0) {
        fprintf(stderr, "Steps per revolution must be non-zero.\n");
        return -EINVAL;
    }

    if (arguments->start == arguments->end) {
        fprintf(stderr, "start and end position are the same.\n");
    }

    int ret = motor_motion_stepper_init_context_struct(arguments->start, arguments->end, arguments->max_velocity,
                                                       arguments->max_acceleration, arguments->min_step,
                                                       arguments->timer_increment, &stepper_context);
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize stepper context: %d\n", ret);
        print_context_variables(&stepper_context.motion_profile);
        return ret;
    }

    ssize_t n_vals = motor_motion_stepper_generate_timing_table(stepper_values, STEPPER_VALUES_NUM, &stepper_context);
    ssize_t j = 0;
    while (n_vals > 0) {
        fprintf(stderr, "Run %ld of size %ld\n", j++, n_vals);
        if (arguments->verify_max_velocity || arguments->verify_max_acceleration) {
            stepper_verify_max_velocity_and_acceleration(&stepper_context, stepper_values, ret,
                                                         arguments->verify_max_velocity,
                                                         arguments->verify_max_acceleration);
        }
        if (arguments->print) {
            print_stepper_values(&stepper_context, stepper_values, ret);
        }
        n_vals = motor_motion_stepper_generate_timing_table(stepper_values, STEPPER_VALUES_NUM, &stepper_context);
    }

    if (n_vals < 0) {
        fprintf(stderr, "Failed to generate servo values: %d\n", ret);
        return -1;
    }

    return 0;
}

int main(const int argc, char *argv[]) {
    static struct arguments arguments = {0};
    static stepper_motor_context_t stepper_context = {0};
    static struct argp argp = {options, parse_opt, args_doc, doc};

    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    if (arguments.servo) {
        const int ret = model_and_verify_servo(&arguments);
        if (ret < 0) {
            fprintf(stderr, "Error in servo model: %d\n", ret);
        }
    }

    if (arguments.stepper) {
        const int ret = model_and_verify_stepper(&arguments);
        if (ret < 0) {
            fprintf(stderr, "Error in stepper model: %d\n", ret);
        }
    }

    return 0;
}