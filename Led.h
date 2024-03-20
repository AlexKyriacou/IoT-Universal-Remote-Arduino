#ifndef LED_H
#define LED_H

class Led {
    private:
        int pin;
        int state;

    public:
        Led(int pin);
        void on();
        void off();
        void toggle();
};

#endif