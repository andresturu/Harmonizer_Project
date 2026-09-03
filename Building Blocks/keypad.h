// keypad.h

#ifndef KEYPAD
#define KEYPAD

extern int keypad[3][3];

struct KeyEvent
{
    bool pressed;
    int row;
    int col;
    int keyID;
};

extern void setUpKeypad();

// updates keypad and returns true if keypad chnaged state
extern KeyEvent checkKeypad();

extern void printKeypad();

#endif