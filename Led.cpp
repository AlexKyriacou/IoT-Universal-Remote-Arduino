#include "Led.h"
#include <Arduino.h>

Led::Led(int pin) : pin(pin), state(LOW)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

void Led::on()
{
    digitalWrite(pin, HIGH);
}

void Led::off()
{
    digitalWrite(pin, LOW);
}

void Led::toggle()
{
    digitalWrite(pin, !digitalRead(pin));
}