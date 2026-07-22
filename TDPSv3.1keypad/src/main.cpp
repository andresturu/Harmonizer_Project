#include <Arduino.h>
#include <keypad.h>
#include <iostream>
#include <string>

const int num_buttons = 9;
// first button in notes is the tonic
bool notes[num_buttons];

//[1,0,0,0] // 100% original toni
//[1, 1, 0, 1] //40% original tonic, rest 60% is split up between other two

void setup()
{
  Serial.begin(115200);
  setUpKeypad();

  notes[0] = 1;
  for (int i = 1; i < num_buttons; i++)
  {
    notes[i] = 0;
  }

}


void loop()
{
  // returns KeyEvent, with attributes .pressed, .row, .col, and .keyID
  KeyEvent keyevent = checkKeypad();

  // if a key was pressed, process
  if (keyevent.pressed)
  {
    // if reset button is pressed, reset to only playing the tonic
    if (keyevent.keyID == 1)
    {
      std::cout << "reset vector" << std::endl;
      
      notes[0] = 1;
      for (int i = 1; i < num_buttons; i++)
      {
        notes[i] = 0;
      }
    }
    // if button other than the reset button is pressed, add button to notes
    else
    {
      std::cout << "add another harmony note\n";
      notes[keyevent.keyID] = 1;
    }
    
    printKeypad();
    for (int note : notes) {
      std::cout << note << " " ;
    }
    std::cout << "\n";
    
  }

}
