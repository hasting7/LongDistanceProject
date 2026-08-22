#include "driver/gpio.h"

#define MAX_PINS (16)

static int pins[MAX_PINS];
static int used_pins;

void gpio_power(int pin, bool on) {
    gpio_set_level(pin, on ? 1 : 0);
}

bool gpio_register_output(int pin) {
    if (used_pins >= MAX_PINS) return false;

    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);

    pins[used_pins++] = pin;

    return true;
}
