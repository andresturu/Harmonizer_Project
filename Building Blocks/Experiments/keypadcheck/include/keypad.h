// keypad.h

#ifndef KEYPAD
#define KEYPAD

// just keeps real-time 0 for unpressed, 1 for pressed
extern int keypad[4][3];

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