/* --- Display.h --- */

/* ------------------------------------------
Author: Matt
Date: 20/11/2026
------------------------------------------ */

#ifndef DISPLAY_H
#define DISPLAY_H
#include <Arduino.h>

class Display {
public:
    Display();
    ~Display();
    static const uint8_t COL = 16;
    static const uint8_t ROW = 2;

    static const uint8_t HIHATS = 0b001;
    static const uint8_t SNARE = 0b010;
    static const uint8_t BASS = 0b100;

    void CreerCaractere(uint8_t insturments);
    void AddPas(uint8_t col, uint8_t instruments);
    void AddSynth(char note, uint8_t col);



private:

};

#endif // DISPLAY_H
