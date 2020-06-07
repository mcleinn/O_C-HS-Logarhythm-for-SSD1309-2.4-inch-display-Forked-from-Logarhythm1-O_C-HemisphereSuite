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

    // Icons made with http://beigemaze.com/bitmap8x8.html  (Thanks for making this public!)
    const uint8_t STAIRS_ICON[8] = {0x00,0x20,0x20,0x38,0x08,0x0e,0x02,0x02};  // Some stairs going up
    const uint8_t RANDOM_ICON[8] = {0x7c,0x82,0x8a,0x82,0xa2,0x82,0x7c,0x00};  // A die showing '2'

    const char* applet_name() {
        return "Stairs";
    }

    void Start() {
        steps = 1;
        dir = 0;
        rand = 0;
        cursor = 0;
        curr_step = 0;
        
        step_cv_lock = 0;
        position_cv_lock = 0;
        reset_held_step = 0;
        reset_gate = 0;
        //rand_anim_time = 0;
        
        reverse = 0;
        cv_out = 0;
        cv_rand = 0;
    }

    void Controller() {

        int curr_step_pv = curr_step;  // Detect if an input changes the step this update
        
        // CV input 0 (Step count)

        step_cv_lock = 0;  // Track if cv is controlling the step count, for display
        if(DetentedIn(0) > 0)   // Is CV greater than 0v by a deadzone amount?
        {
          int num = ProportionCV(In(0), HEM_STAIRS_MAX_STEPS);  // Use this range so it's easy to reach max-1 just before 5v
          num = constrain(num, 0, HEM_STAIRS_MAX_STEPS-1);      // Constrain to max-1
          steps = num;  // Just overwrite user values
          step_cv_lock = 1;  // Display this since it locks out user input
        }
  
        // CV input 1 (Position control)
        position_cv_lock = 0;  // Track if position is under cv control
        if(DetentedIn(1) > 0)   // Is CV greater than 0v by a deadzone amount?
        {
          // TODO: Handle up/down mode by (steps/2 - 2)?
          int num = ProportionCV(In(1), steps);
          num = constrain(num, 0, steps);
          curr_step = num;  // Just overwrite?  (TODO: Preferable to modulate a set point?)
          position_cv_lock = 1;
        }


        // Digital Input 1: Reset pulse
        reset_gate = Gate(1);  // For display
        if (Clock(1)) {
            reset_held_step = curr_step;  // For display
            curr_step = (dir != 2) ? 0 : steps;  // Go to 0th or last step depending on direction
            reverse = (dir != 2) ? 0 : 1;  // Reset reverse (really just for up/down mode)
            ClockOut(1);  // BOC pulse output
        }
        
        // Digital Input 2: Clock pulse
        if (Clock(0) && !reset_gate && !position_cv_lock)  // Don't clock if currently within a reset pulse, so overlapping clock+reset pulses go to step 0 instead of 1 and reset can "hold"
        {
            if(reverse == 0)
            {
              // Forward direction
              if(++curr_step > steps)
              {
                if(dir == 0)  // up
                {
                  curr_step = 0;
                  // In up mode, BOC should trigger when looping back to step 0
                  ClockOut(1);
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
              // Reverse direction
              if(curr_step > steps)
              {
                // Total steps have been manually changed to a number below the current position, so clamp
                curr_step = steps;
              }
              else 
              {
                --curr_step;

                // If in up/down mode, BOC should trigger when descending and arriving at step 0
                if(curr_step == 0 && dir == 1)
                {
                  ClockOut(1);
                }
                
                if(curr_step < 0)
                {
                  if(dir == 2) // down
                  {
                    curr_step = steps;
                    // In down mode, BOC puse should trigger when looping back to the end step
                    ClockOut(1);
                  }
                  else  // up/down
                  {
                    reverse = 0;
                    curr_step = (steps > 0 ? 1 : 0);  // Go to 1 unless too few steps
                  }                  
                }                
              }
            }
        }


        // If the step has changed, update anything else that needs to
        // Note: Should BOC pulses be moved here to trigger via CV changing current step?
        if(curr_step != curr_step_pv && !position_cv_lock)
        {
            // Compute a new random offset if required
            if(rand)
            {
              cv_rand = Proportion(1, steps, HEMISPHERE_MAX_CV);  // 0-5v, scaled with fixed-point
              cv_rand = random(0, cv_rand/4);  // Deviate up to 1/x step amount
              // Randomly choose offset direction
              cv_rand *= (random(0,100) > 50) ? 1 : -1;

              //rand_anim_time = 12;  // Jostle the die graphic up a bit for a split second
            }
          
        }
        

        if(!reset_gate)  // Don't update cv if reset gate is on-- this gives sample & hold of the last value until reset is released
        {
          // Steps will either be counting up or down, but it will always be an index into the cv range
          cv_out = Proportion(curr_step, steps, HEMISPHERE_MAX_CV);  // 0-5v, scaled with fixed-point
          if(rand && (curr_step != 0 && curr_step != steps))  // Don't randomize 1st and last steps so it always hits 0 and 5v?
          {
            cv_out += cv_rand;
            cv_out = constrain(cv_out, 0, HEMISPHERE_MAX_CV);  // (Not actually necessary if not randomizing start/end)        
          }
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
    int steps;      // Number of steps, starting at 0v and ending at 5v (if > 0 steps)
    int dir;        // 0 = up, 1 = up/down, 2 = down
    int rand;       // 0 = no cv out randomization, 1 = random offsets are applied to each step
    int curr_step;  // Current step
    int reverse;    // current movement direction
    int cv_out;     // CV currently being output (track for display)

    int cv_rand;            // track last computed random offset for cv
    int step_cv_lock;       // 1 if cv is controlling the current step (show on display)
    int position_cv_lock;   // 1 if cv is controlling the current step (show on display)
    int reset_gate;         // Track if currently held in reset (show an icon)
    int reset_held_step;    // Step when reset was initially asserted (for display)
    int rand_anim_time;     // Track wiggling the die when random is applied (for display)
    
    int cursor;     // 0 = steps, 1 = direction, 2 = random

    

    void DrawDisplay()
    {
      gfxBitmap(1, 15, 8, STAIRS_ICON); gfxPrint(16,15,steps);//gfxPrint(16,15,steps);
      //if(cursor == 0) gfxCursor(16, 23, 15);  // flashing underline on the number
      
      //if(step_cv_lock) gfxBitmap(12, 15, 8, CV_ICON);
      if(step_cv_lock)
      {
        gfxBitmap(16, 25, 8, CV_ICON);
        //gfxInvert(16, 14, 15, 9);  // After cursor
      }
      

      
      //int y = 15;
      //gfxBitmap(1, y, 8, STAIRS_ICON);
      //gfxPrint(9 + pad(100,curr_step), y, curr_step); gfxPrint("/");gfxPrint(steps);  // Pad x enough to hold width steady  // Tested: Makes it hard to interact w/max steps visually
      //if(reset_gate) gfxBitmap(56, 15, 8, RESET_ICON);
      
      //if(step_cv_lock) gfxBitmap(50, 15, 8, CV_ICON);
      //gfxBitmap(50, 15, 8, CV_ICON);  // Works if always shown

      // Test dir icon on same line as steps
      //gfxBitmap(36, 15, 8, (dir==0 ? UP_BTN_ICON : ( dir==1 ? UP_DOWN_ICON : DOWN_BTN_ICON )));
      gfxBitmap(38, 15, 8, (dir==0 ? UP_BTN_ICON : ( dir==1 ? UP_DOWN_ICON : DOWN_BTN_ICON )));
      
      
      // direction on its own line:
      //gfxBitmap(1, 25, 8, LOOP_ICON);
      //gfxBitmap(16, 25, 8, (dir==0 ? UP_BTN_ICON : ( dir==1 ? UP_DOWN_ICON : DOWN_BTN_ICON )));
      

      // Just to see this:
      //gfxBitmap(55, 25, 8, UP_DOWN_ICON);

      
      /*
      // When it applies, animate a jump on the random icon
      int die_y = 35;
      if(rand_anim_time > 0)  // Set when random is applied in Controller() update
      {
        --rand_anim_time;
        die_y = 33;
      }
      gfxBitmap(1, die_y, 8, RANDOM_ICON);
      */
      gfxBitmap(1, 35, 8, RANDOM_ICON);
      if(!rand)
      {
        gfxPrint(16,35, "off");
      }
      else
      {
        gfxPrint(16,35, "rnd");
        //gfxPrint(1, 35, "r"); gfxPos(12, 35); gfxPrintVoltage(cv_rand);
        //gfxInvert(16, 35, ProportionCV(cv_rand, 40), 9);  // will this flip on its own?
        //gfxPos(16, 35); gfxPrintVoltage(cv_rand);  // Numeric readout for testing
      }
      
      //if(reset_gate) gfxBitmap(1, 45, 8, RESET_ICON);
      //gfxPrint(9 + pad(100,curr_step), 45, curr_step); gfxPrint("/");gfxPrint(steps);  // Pad x enough to hold width steady
      int display_step = reset_gate ? reset_held_step : curr_step;
      gfxPrint(6+pad(100,display_step), 55, display_step); gfxPrint("/");gfxPrint(steps);  // Pad x enough to hold width steady
      if(reset_gate)
      {
        gfxBitmap(1, 55, 8, RESET_ICON);  // Indicate that Reset is holding the step
      }
      
      if(position_cv_lock) 
      {
        gfxBitmap(13, 45+3, 8, CV_ICON);  // Indicate that CV is holding the step
      }

      
      //gfxBitmap(1, 55, 8, CV_ICON); gfxPos(12, 55); gfxPrintVoltage(cv_out);  // Numeric readout for testing
      
      // Level indicator
      //gfxInvert(9, 55, ProportionCV(cv_out, 54), 9);

      // Try up/down indicator:
      int h = 1+ProportionCV(cv_out, 46);  // Always show 1 
      //gfxInvert(52, 63-h, 9, h);
      gfxInvert(48, 63-h, 9, h);

      // Cursor
      //gfxCursor(1, 23 + (cursor * 10), 62);  // This is a flashing underline cursor for the whole row when used like this
      if(cursor == 0)
      {
        gfxCursor(16, 23, 15);  // flashing underline on the number
      }
      else if(cursor == 1)
      {
        
        //gfxCursor(16, 23 + (cursor * 10), 18);  // flashing underline on the number
        gfxCursor(38, 23, 9);  // flashing underline on up/down icon
      }
      else
      {
        gfxCursor(16, 23 + (cursor * 10), 20);  // flashing underline on the random setting
      }
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

