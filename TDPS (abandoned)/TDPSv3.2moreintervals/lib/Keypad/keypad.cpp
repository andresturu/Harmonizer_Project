#include "keypad.h"
#include <Arduino.h>

#define R0_pin 13
#define R1_pin 14
#define R2_pin 27
#define C0_pin 25
#define C1_pin 33
#define C2_pin 32

const int numRows = 3;
const int numCols = 3;
const unsigned long debounceDelay = 20;

int rowPins[] = {R0_pin, R1_pin, R2_pin};
int colPins[] = {C0_pin, C1_pin, C2_pin};

int keyIds[3][3] = {
    {1, 2, 3},
    {4, 5, 6},
    {7, 8, 9}};

unsigned long lastDebounceTimes[numRows][numCols];

// 0
int lastRawStates[numRows][numCols];

// 0 means pressed, 1 means unpressed
int keypad[numRows][numCols];

// Helper function to print the current state of the matrix
void printKeypad()
{
    Serial.println("--- Key Matrix State ---");
    for (int i = 0; i < numRows; i++)
    {
        for (int j = 0; j < numCols; j++)
        {
            Serial.print(keypad[i][j] ? "1 " : "0 ");
        }
        Serial.println(); // New line at the end of each row
    }
    Serial.println("------------------------");
}

void setUpKeypad()
{
    // initialize rows with pulup resitors
    for (int i = 0; i < numRows; i++)
    {
        pinMode(rowPins[i], INPUT_PULLUP);
    }

    // initialize columns to be high init
    for (int j = 0; j < numCols; j++)
    {
        pinMode(colPins[j], OUTPUT);
        digitalWrite(colPins[j], HIGH);
    }

    // initialize keys to not be pressed, since rows are pulled high 1 means unpressed
    for (int i = 0; i < numRows; i++)
    {
        for (int j = 0; j < numCols; j++)
        {
            keypad[i][j] = 0;
            lastRawStates[i][j] = 1;
            lastDebounceTimes[i][j] = 0;
        }
    }
}

// return KeyEvent containing details of FIRST button press, if it happens at all
KeyEvent checkKeypad()
{
    // Serial.println("in loop");

    static int raw_reading;
    static int reading;

    KeyEvent keyevent{false, -1, -1, -1};

    // loop through cols
    for (int j = 0; j < numCols; j++)
    {
        // set column to low
        digitalWrite(colPins[j], LOW);

        // loop through rows
        for (int i = 0; i < numRows; i++)
        {
            raw_reading = digitalRead(rowPins[i]); // 1 means unpressed
            reading = 1 - raw_reading;             // 1 means pressed

            // triggers if change has happened, whether from touch or debouncing
            // resets lastDebounceTime while signal is not stable
            if (raw_reading != lastRawStates[i][j])
            {
                // reset the debouncing timer
                lastDebounceTimes[i][j] = millis();
            }

            // once lastDebounceTime hasn't changed in a while, set definitive keypad
            if ((millis() - lastDebounceTimes[i][j]) > debounceDelay)
            {
                // if the button state has actually changed
                if (reading != keypad[i][j])
                {
                    // update keypad
                    keypad[i][j] = reading;

                    // if button was pressed, AND this is the first button to be pressed in loop
                    if (reading && !keyevent.pressed)
                    {
                        keyevent.pressed = true;
                        keyevent.row = i;
                        keyevent.col = j;
                        keyevent.keyID = keyIds[i][j];
                    }
                }
            }

            // update last raw state
            lastRawStates[i][j] = raw_reading;
        }

        // reset column back to high
        digitalWrite(colPins[j], HIGH);
    }
    return keyevent;
}
