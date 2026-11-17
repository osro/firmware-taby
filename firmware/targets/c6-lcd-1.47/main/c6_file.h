#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
bool c6_file_read(FILE *file, uint32_t base, uint32_t length,
                  size_t offset, void *out, size_t size);
