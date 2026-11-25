#include <Arduino.h>
#include "Led.h"


void setup() {
  Led led(5);
  led.begin();
  delay(1000);
  led.AllumeLed();
  delay(1000);
  led.EteintLed();
  delay(1000);
  led.ToogleLed();  
}

void loop() {
  
}