// Copyright (c) 2020, Logarhythm
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// This Hemisphere applet is based on the Noise Engineering module "Clep Diaz."
// A number of steps (n) is set, and each clock input pulse causes the output to increase or decrease by 1/n of the max voltage
// depending on the direction setting of up, up/down, or down.
// If up/down mode is specified, the number of steps will be nearly doubled, but won't repeat the bottom and top steps (2n-2 steps.)
// Enabling "random" will cause each step value to deviate randomly by some % within the range of the prior and next step,
// keeping the direction of change but avoiding precise repetition.
// This version provides unipolar 0-5v output, divided into n steps (O&C hardware can only output -3v to +6v so this seemed sensible.)


#define HEM_STAIRS_MAX_STEPS 32

class Stairs : public HemisphereApplet {
public:

    const char* applet_name() {
        return "Stairs";
    }

    void Start() {
        steps = 1;
        dir = 0;
        rand = 0;
        cursor = 0;
        curr_step = 0;
        reverse = 0;
        cv_out = 0;

        cv_rand = 0;
    }

    void Controller() {

        // Reset input
        if (Clock(1)) {
            curr_step = (dir != 2) ? 0 : steps;  // Go to 0th or last step depending on direction
            ClockOut(1);  // BOC output
        }
                // Clock input
        else if (Clock(0))// StartADCLag();   // TODO: Might help reset behavior, so check this
        //if (EndOfADCLag()) {
        {
            if(reverse == 0)
            {
              // Forwards
              if(++curr_step > steps)
              {
                if(dir == 0)  // up
                {
                  curr_step = 0;
                  ClockOut(1);  // BOC output
                }
                else  // up/down
                {
                  reverse = 1;
                  curr_step = (steps > 0 ? steps-1 : 0);  // Go to step before last unless too few steps
                }
              }

            }
            else
            {
              // Reverse
              if(curr_step > steps)
              {
                // Total steps have been manually changed to a number below the current position, so clamp
                curr_step = steps;
              }
              else if(--curr_step < 0)
              {
                if(dir == 2) // down
                {
                  curr_step = steps;
                }
                else  // up/down
                {
                  reverse = 0;
                  curr_step = (steps > 0 ? 1 : 0);// constrain(1, 0, steps);  // Go to 1 unless too few steps
                }

                ClockOut(1);  // BOC output (for both down and up/down)
              }
            }

            // Compute a new random offset if required
            if(rand)
            {
              cv_rand = Proportion(1, steps, HEMISPHERE_MAX_CV);  // 0-5v, scaled with fixed-point
              cv_rand = random(0, cv_rand/4);  // Deviate up to 1/x step amount
              // Randomly choose offset direction
              cv_rand *= (random(0,100) > 50) ? 1 : -1;
            }
        }

        // Steps will either be counting up or down, but it will always be an index into the cv range
        cv_out = Proportion(curr_step, steps, HEMISPHERE_MAX_CV);  // 0-5v, scaled with fixed-point
        if(rand && (curr_step != 0 && curr_step != steps))  // Don't randomize 1st and last steps so it always hits 0 and 5v?
        {
          cv_out += cv_rand;
          cv_out = constrain(cv_out, 0, HEMISPHERE_MAX_CV);  // (Not actually necessary if not randomizing start/end)        
        }

        Out(0, cv_out);
    }

    void View() {
        gfxHeader(applet_name());
        DrawDisplay();
    }

    void OnButtonPress() {
      if(++cursor > 2) cursor = 0;
    }

    void OnEncoderMove(int direction) {
        if (cursor == 0) 
        {
            steps = constrain( steps += direction, 0, HEM_STAIRS_MAX_STEPS-1);  // constrain includes max
        }
        else if (cursor == 1) 
        {
            dir = constrain( dir += direction, 0, 2);

            // Don't change current direction if up/down mode
            if(dir != 1)
            {
              reverse = (dir == 2);  // Change current trend to up or down if required
            }
        } 
        else 
        {
            rand = 1-rand;
        }

    }
        
    uint32_t OnDataRequest() {
        uint32_t data = 0;
        Pack(data, PackLocation {0, 5}, steps);
        Pack(data, PackLocation {5, 2}, dir);
        Pack(data, PackLocation {7, 1}, rand);
        return data;
    }

    void OnDataReceive(uint32_t data) {
        steps = Unpack(data, PackLocation {0, 5});
        dir = Unpack(data, PackLocation {5, 2});
        rand = Unpack(data, PackLocation {7, 1});

        // Init from received data:
        reverse = (dir == 2);  // Set reverse if starting in down direction
    }

protected:
    void SetHelp() {
    //                                    "------------------" <-- Size Guide      
        help[HEMISPHERE_HELP_DIGITALS] =  "1=Clock 2=Reset";
        help[HEMISPHERE_HELP_CVS] =       "1=Steps 2=Position";
        help[HEMISPHERE_HELP_OUTS] =      "A=CV B=BOC Trg";
        help[HEMISPHERE_HELP_ENCODER] =   "Steps/Dir/Rand";
    //                                    "------------------" <-- Size Guide       
    }

private:
    int steps;     // Number of steps, starting at 0v and ending at 5v (if > 0 steps)
    int dir;       // 0 = up, 1 = up/down, 2 = down
    int rand;      // 0 = no cv out randomization, 1 = random offsets are applied to each step
    int curr_step;  // Current step
    int reverse;  // current movement direction
    int cv_out;   // CV currently being output (track for display)

    int cv_rand;  // track last computed random offset for cv
    int cursor;    // 0 = steps, 1 = direction, 2 = random

    void DrawDisplay()
    {
      gfxPrint(1, 15, "Steps: "); gfxPrint(steps);
      gfxPrint(1, 25, "Dir: "); gfxPrint((dir == 0 ? "up" : (dir == 1 ? "up/dn" : "down")));
      //gfxPrint(1, 35, ": "); gfxPrint(rand);
      if(!rand)
      {
        gfxPrint(1, 35, "Rand: Off");
      }
      else
      {
        gfxPrint(1, 35, "r"); gfxPos(12, 35); gfxPrintVoltage(cv_rand);
      }
      
      gfxPrint(1 + pad(100,curr_step), 45, curr_step); gfxPrint("/");gfxPrint(steps);  // Pad x enough to hold width steady
      gfxBitmap(1, 55, 8, CV_ICON); gfxPos(12, 55); gfxPrintVoltage(cv_out);


      // Cursor
      gfxCursor(1, 23 + (cursor * 10), 62);  // This is a flashing underline cursor for the whole row when used like this
    }
    
    //void DrawDisplay() {
      //  segment.SetPosition(11 + (hemisphere * 64), 32);
       // for (int b = 0; b < 4; b++)
        //{
        //    segment.PrintDigit(static_cast<uint8_t>(bit[b]));
        //}
        //gfxRect(1, 15, ProportionCV(ViewOut(0), 62), 6);
        //gfxRect(1, 58, ProportionCV(ViewOut(1), 62), 6);
    //}
};


////////////////////////////////////////////////////////////////////////////////
//// Hemisphere Applet Functions
///
///  Once you run the find-and-replace to make these refer to Stairs,
///  it's usually not necessary to do anything with these functions. You
///  should prefer to handle things in the HemisphereApplet child class
///  above.
////////////////////////////////////////////////////////////////////////////////
Stairs Stairs_instance[2];

void Stairs_Start(bool hemisphere) {
    Stairs_instance[hemisphere].BaseStart(hemisphere);
}

void Stairs_Controller(bool hemisphere, bool forwarding) {
    Stairs_instance[hemisphere].BaseController(forwarding);
}

void Stairs_View(bool hemisphere) {
    Stairs_instance[hemisphere].BaseView();
}

void Stairs_OnButtonPress(bool hemisphere) {
    Stairs_instance[hemisphere].OnButtonPress();
}

void Stairs_OnEncoderMove(bool hemisphere, int direction) {
    Stairs_instance[hemisphere].OnEncoderMove(direction);
}

void Stairs_ToggleHelpScreen(bool hemisphere) {
    Stairs_instance[hemisphere].HelpScreen();
}

uint32_t Stairs_OnDataRequest(bool hemisphere) {
    return Stairs_instance[hemisphere].OnDataRequest();
}

void Stairs_OnDataReceive(bool hemisphere, uint32_t data) {
    Stairs_instance[hemisphere].OnDataReceive(data);
}
/*
#define HEM_STAIRS_MAX_STEPS 32

class Stairs : public HemisphereApplet {
public:

    const char* applet_name() {
        return "Stairs";
    }

    void Start() {
        steps = 1;
        dir = 0;
        rand = 0;
        cursor = 0;
        curr_step = 0;
        cv_out = 0;
    }

  void Controller() {
        
        // Reset input
        if (Clock(1)) {
            curr_step = 0;
            ClockOut(1);  // BOC output
        }

        //  TODO: Handle live changes to settings maybe this way
        //int transpose = 0;
        //if (DetentedIn(0)) {
          //  transpose = In(0) / 128; // 128 ADC steps per semitone
        //}
        //int play_note = note[curr_step] + 60 + transpose;
        //play_note = constrain(play_note, 0, 127);
        


        // Clock input
        if (Clock(0))// StartADCLag();   // TODO: Might help reset behavior, so check this
        //if (EndOfADCLag()) {
        {
            //Advance(curr_step);
            
            if(++curr_step >= steps)
            {
              curr_step = 0;
              ClockOut(1);  // BOC output
            }
            // play = 1;
        }

        // TODO: Vary for up+down (in case of up/dn mode, a bit should signify up or down states)

        // Fixed-point maths
        cv_out = Proportion(curr_step, steps, HEMISPHERE_MAX_CV);  // This appears to be 5v from code, TODO: Confirm
        Out(0, cv_out);

    }

    void View() {
        gfxHeader(applet_name());

        DrawSelector();
        
        //DrawSkewedWaveform();
        //DrawRateIndicator();
        //DrawWaveformPosition();
    }

    void OnButtonPress() {
        cursor = constrain( cursor++, 0 , 2);
    }

    void OnEncoderMove(int direction) {
        if (cursor == 0) {
            steps = constrain( steps += direction, 0, HEM_STAIRS_MAX_STEPS-1);  // constrain includes max
        }else if (cursor == 1) {
            dir = constrain( dir += direction, 0, 2);
        } else {
            rand = 1-rand;
        }
    }

    uint32_t OnDataRequest() {
        uint32_t data = 0;
        Pack(data, PackLocation {0, 5}, steps);
        Pack(data, PackLocation {5, 2}, dir);
        Pack(data, PackLocation {7, 1}, rand);
        return data;
    }

    void OnDataReceive(uint32_t data) {
        steps = UnPack(data, PackLocation {0, 5});
        dir = UnPack(data, PackLocation {5, 2});
        rand = UnPack(data, PackLocation {7, 1});
         //skew = Unpack(data, PackLocation {0,8});
        //rate = Unpack(data, PackLocation {8,8});
    }

protected:
    void SetHelp() {
    //                                    "------------------" <-- Size Guide      
        help[HEMISPHERE_HELP_DIGITALS] =  "1=Clock 2=Reset";
        help[HEMISPHERE_HELP_CVS] =       "1=Steps 2=Position";
        help[HEMISPHERE_HELP_OUTS] =      "A=CV B=EOC_Pulse";
        help[HEMISPHERE_HELP_ENCODER] =   "Steps/Dir/Rand";
    //                                    "------------------" <-- Size Guide          
    }

private:
    int steps;     // Number of steps, starting at 0v and ending at 5v (if > 0 steps)
    int dir;       // 0 = up, 1 = up/down, 2 = down
    int rand;      // 0 = no cv out randomization, 1 = random offsets are applied to each step

    int curr_step;  // Current step
    int cv_out;

    void DrawSelector()
    {
      gfxPrint(1, 15, "Steps:"); gfxPrint(9, 15, steps);
      gfxPrint(1, 25, "Dir:"); gfxPrint(9, 25, (dir == 0 ? "up" : (dir == 1 ? "up/down" : "down")));
      gfxPrint(3, 35, "Rand:"); gfxPrint(9, 35, rand);
      
      gfxPrint(3, 45, "pos="); gfxPrint(10, 45, curr_step);
      gfxBitmap(1, 55, 8, CV_ICON); gfxPos(12, 55); gfxPrintVoltage(cv_out);


      // Cursor

      gfxCursor(1, 23 + (cursor * 10), 62);
    }
};


////////////////////////////////////////////////////////////////////////////////
//// Hemisphere Applet Functions
///
///  Once you run the find-and-replace to make these refer to Stairs,
///  it's usually not necessary to do anything with these functions. You
///  should prefer to handle things in the HemisphereApplet child class
///  above.
////////////////////////////////////////////////////////////////////////////////
Stairs Stairs_instance[2];

void Stairs_Start(bool hemisphere) {
    Stairs_instance[hemisphere].BaseStart(hemisphere);
}

void Stairs_Controller(bool hemisphere, bool forwarding) {
    Stairs_instance[hemisphere].BaseController(forwarding);
}

void Stairs_View(bool hemisphere) {
    Stairs_instance[hemisphere].BaseView();
}

void Stairs_OnButtonPress(bool hemisphere) {
    Stairs_instance[hemisphere].OnButtonPress();
}

void Stairs_OnEncoderMove(bool hemisphere, int direction) {
    Stairs_instance[hemisphere].OnEncoderMove(direction);
}

void Stairs_ToggleHelpScreen(bool hemisphere) {
    Stairs_instance[hemisphere].HelpScreen();
}

uint32_t Stairs_OnDataRequest(bool hemisphere) {
    return Stairs_instance[hemisphere].OnDataRequest();
}

void Stairs_OnDataReceive(bool hemisphere, uint32_t data) {
    Stairs_instance[hemisphere].OnDataReceive(data);
}
*/
