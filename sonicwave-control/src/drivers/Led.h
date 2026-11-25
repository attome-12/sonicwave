/* --- Led.h --- */

/* ------------------------------------------
Author: Matt
Date: 20/11/2026
------------------------------------------ */

#ifndef LED_H
#define LED_H
#include <Arduino.h>

class Led {
public:
    Led(uint8_t pin);
    ~Led();
    void begin();
    void AllumeLed();
    void EteintLed();
    void ToogleLed();
    void ClignoterLed(int del);

private:
    uint8_t _pin;
    bool _status;
};

#endif // LED_H
