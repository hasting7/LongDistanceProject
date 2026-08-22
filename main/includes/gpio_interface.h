#ifndef _GPIO_INTERFACE_H_
#define _GPIO_INTERFACE_H_

void gpio_power(int pin, bool on);
bool gpio_register_output(int pin);

#endif