#pragma once

#include <stdbool.h>
#include <stdint.h>

// I wanted to try bitwise operands in a project
//
// Indices start from right!!!

void set_bit(uint8_t *word, uint8_t place);

void clear_bit(uint8_t *word, uint8_t place);

void toggle_bit(uint8_t *word, uint8_t place);

bool read_bit(uint8_t *word, uint8_t place);

void print_byte(uint8_t *word);
