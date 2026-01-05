#include <stdio.h>

#include "bits.h"

void set_bit(uint8_t *word, uint8_t place)
{
  *word = *word | (1u << place);
}

void clear_bit(uint8_t *word, uint8_t place)
{
  *word = *word & ~(1u << place);
}

void toggle_bit(uint8_t *word, uint8_t place)
{
  *word = *word ^ (1u << place);
}

bool read_bit(uint8_t *word, uint8_t place)
{
  return (*word & (1u << place)) > 0 ? true : false;
}

void print_byte(uint8_t *word)
{
  printf("Decimal Int: %u\n", *word);
  char byte[8] = {0};

  for (uint8_t i = 0; i < 8; ++i)
    byte[8 - i - 1] = read_bit(word, i) ? '1' : '0';

  printf("Binary Int: %.*s", 8, byte);
}
