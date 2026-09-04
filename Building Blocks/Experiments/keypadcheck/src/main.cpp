#include <Arduino.h>
#include "keypad.h"



void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  setUpKeypad();
}

void loop() {
  checkKeypad();
  printKeypad();
  delay(50);
}

