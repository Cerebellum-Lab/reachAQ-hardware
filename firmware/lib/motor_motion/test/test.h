#pragma once

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern int print_n_iterations;

#define PRINT_ERR(...)            \
    fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr);

#define LOG_ERR(...) PRINT_ERR(__VA_ARGS__)
#define LOG_WRN(...) PRINT_ERR(__VA_ARGS__)
#define LOG_DBG(...) PRINT_ERR(__VA_ARGS__)
#define LOG_INF(...) PRINT_ERR(__VA_ARGS__)
#define BUILD_ASSERT(cond, str) assert(cond)