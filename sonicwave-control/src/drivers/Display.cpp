/* --- Display.cpp --- */

/* ------------------------------------------
author: Matt
date: 20/11/2026
------------------------------------------ */

#include "Display.h"

Display::Display() {
    // Constructor
}

Display::~Display() {
    // Destructor
}

void Display::CreerCaractere(uint8_t insturments) {
    //Cas 1 : rien
    uint8_t vide[8]={
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000
    };

    //Cas 2 : hi-hats
    uint8_t hiHats[8]={
        0b11111,
        0b11111,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000        
   };

   //Cas 3 : snare
    uint8_t snare[8]={
        0b00000,
        0b00000,
        0b00000,
        0b11111,
        0b11111,
        0b00000,
        0b00000,
        0b00000
   };
};

void Display::AddPas(uint8_t col, uint8_t instruments) {
    
}

void Display::AddSynth(char note, uint8_t col) {
    
}

