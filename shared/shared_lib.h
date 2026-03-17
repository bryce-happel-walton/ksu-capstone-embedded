#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct __attribute__((packed))
{
    char hello[32];
    uint32_t beep;
    bool boop;
} TestData;
