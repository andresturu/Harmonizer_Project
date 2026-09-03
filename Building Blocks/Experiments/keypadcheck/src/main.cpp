#include <Arduino.h>


#define R0_pin 13
#define R1_pin 14
#define R2_pin 27
#define R3_pin 26
#define C0_pin 25
#define C1_pin 33
#define C2_pin 32


#define power 32
#define ground 33

void setup() {
  // put your setup code here, to run once:
  pinMode(power, OUTPUT);
  pinMode(ground, OUTPUT);
}

void loop() {
  digitalWrite(power, HIGH);
  digitalWrite(ground, LOW);
}

