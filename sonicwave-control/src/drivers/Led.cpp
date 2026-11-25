/* --- Led.cpp --- */

/* ------------------------------------------
author: Matt
date: 20/11/2026
------------------------------------------ */

#include "Led.h"

Led::Led(uint8_t pin) : _pin(pin){
    // Constructor
}

Led::~Led() {
    // Destructor
}

void Led::begin() {
    pinMode(_pin, OUTPUT);
    _status = false;
};

void Led::AllumeLed() {
    digitalWrite(_pin, HIGH);
    _status = true;
};

void Led::EteintLed() {
    digitalWrite(_pin, LOW);
    _status = false;
};

void Led::ToogleLed(){
    if (_status) {
        EteintLed();
    } else {
        AllumeLed();
    }
};