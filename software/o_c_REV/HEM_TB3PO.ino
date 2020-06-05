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


// TB-3PO Hemisphere Applet
// A random generator of TB-303 style acid patterns, closely following 303 gate timings
// CV output 1 is pitch, CV output 2 is gates
// CV pitch output (1) includes fixed-time exponential pitch slides timed as on 303s
// CV gates are output at 3v for normal notes and 5v for accented notes
// TODO: Output proper envelopes?

#include "HSMIDI.h"


#define ACID_MAX_STEPS 16


class TB_3PO : public HemisphereApplet {
public:

    const char* applet_name() { // Maximum 10 characters
        return "TB-3PO";
    }

    void Start() 
    {
    	
    	seed = random(0, 65535); // 16 bits
    	regenerate(seed);
          
    	play = 1;
    }

    void Controller() 
    {
      // Regenerate
      if (Clock(1)) 
      {
        
        // TODO: debounce this with some kind of delay
        seed = random(0, 65535); // 16 bits
        regenerate(seed);
        //step = 0;
        //ClockOut(1);
      }
      
      int transpose = 0;
      if (DetentedIn(0)) 
      {
        transpose = In(0) / 128; // 128 ADC steps per semitone
      }
      int play_note = notes[step] + 60 + transpose;
      play_note = constrain(play_note, 0, 127);
      
      // Wait for the ADC since transpose CV is needed
      if (Clock(0)) StartADCLag();
      
      if (EndOfADCLag() && !Gate(1))
      {
        
        // Advance the step
        step = get_next_step(step);
        
        // Lookahead to see what's coming next
        //int next_step = get_next_step(step);
        
        //if(step_is_slid(step))
        //{
        // Gate should be held open until the next step instead of usual duty cycle
        
        
        //if(step_is_slid(next_step))
        //{
        //}
        //}
        
        if(step_is_gated(step))
          ClockOut(1);
        
        play = 1;
      }
      
      
      // TODO: Apply CV density directly to regenerate gates, if int change?
      // TODO: Maybe instead, this cv live-controls a gate skip % chance? (And also sets density when regenerating)
      
      
      if (play) 
      {
      
      
        // Note: If prior step was slid, the gate should still be open, and this pitch change should be slid
        
        // TODO: Set a target pitch instead for slides, with expo 
        int cv = MIDIQuantizer::CV(play_note);
        cv += curr_glide_cv;
        Out(0, cv);
        
        
        if(step_is_gated(step))
        {
          if(step_is_slid(step))
          {
          // Set a flag to hold the gate open until next step
          }
          
          
          // TODO: Fix
          // For now just output a clock when there is a gate on the step			
          //GateOut(1);
        }
      }
    }

    void View() {
      gfxHeader(applet_name());
      DrawSelector();
    }

    void OnButtonPress() {
      if (++cursor > 2) cursor = 0;
    }

    void OnEncoderMove(int direction) 
    {
      if (cursor == 0)
      {
        // Scale selection
        scale += direction;
        if (scale >= OC::Scales::NUM_SCALES) scale = 0;
        if (scale < 0) scale = OC::Scales::NUM_SCALES - 1;
        //quantizer[ch].Configure(OC::Scales::GetScale(scale[ch]), 0xffff);
        //continuous[ch] = 1; // Re-enable continuous mode when scale is changed
      } 
      else if(cursor == 1)
      {
        // Root selection
        root = constrain(root + direction, 0, 11);
      }
      else
      {
      	// Set for the next time a pattern is generated
      	density = constrain(density + direction, 0, 10);
      	
      	
      	// TODO: Maybe instead of altering the existing gates, just set a mask to logical-AND against the existing pattern?
      	// Apply the new density live
      	//apply_density();  // TODO: Move to update loop to react to cv as well?
      }
    }

    uint32_t OnDataRequest() {
        uint32_t data = 0;
		
        Pack(data, PackLocation {0,8}, scale);
        Pack(data, PackLocation {8,4}, root);
        Pack(data, PackLocation {12,4}, density);
        Pack(data, PackLocation {16,16}, seed);
        return data;
    }

    void OnDataReceive(uint32_t data) {
		
      scale = Unpack(data, PackLocation {0,8});
      root = Unpack(data, PackLocation {8,4});
      density = Unpack(data, PackLocation {12,4});
      seed = Unpack(data, PackLocation {16,16});
      
      
      root = constrain(root, 0, 11);
      density = constrain(density, 0,10);
      //quantizer.Configure(OC::Scales::GetScale(scale), 0xffff);
      
      
      // Restore all seed-derived settings!
      regenerate(seed);

    }

protected:
    void SetHelp() {
        //                               "------------------" <-- Size Guide
        help[HEMISPHERE_HELP_DIGITALS] = "1=Clock 2=Regen";
        help[HEMISPHERE_HELP_CVS]      = "1=Transp 2=Density";
        help[HEMISPHERE_HELP_OUTS]     = "A=CV+slides B=Gate";
        help[HEMISPHERE_HELP_ENCODER]  = "Scale/Root/Density";
        //                               "------------------" <-- Size Guide
    }
    
private:
  int cursor = 0;
  
  //char seed = 303;  // Seed value that can be saved/restored
  
  int gates = 0; 		// Bitfield of gates;  ((gates >> step) & 1) means gate
  int slides = 0; 	// Bitfield of slide steps; ((slides >> step) & 1) means slide
  int accents = 0; 	// Bitfield of accent steps; ((accents >> step) & 1) means accent
  int notes[ACID_MAX_STEPS];
  int step = 0; // Current sequencer step
  bool play; // Play the note
  
  
  int curr_glide_cv = 0;
  
  uint16_t seed;
  int scale = 0;		// Scale
  uint8_t root = 0;	// Root note
  
  uint8_t density = 10;  // 

	
	
	void regenerate(int seed)
	{
		randomSeed(seed);  // Ensure random()'s seed
		
		regenerate_pitches();
		apply_density();
	}
	
	// Regenerate pitches
	void regenerate_pitches()
	{
		
		// TODO: scale-constrain

    int bag[4] = {0, 12, 1, 7};

    
		for (int s = 0; s < ACID_MAX_STEPS; s++) 
		{
			// Test octs
			//notes[s] = root + rand_bit(40) * 12; //random(0, 30);
     notes[s] = root + bag[random(0,3)];
		}
	
	}
	
	// Change pattern density without affecting pitches
	void apply_density()
	{
		int latest = 0; // Track previous bit for some algos
		
		gates = 0;
		int densProb = density * 10;
		for(int i=0; i<ACID_MAX_STEPS; ++i)
		{
			gates |= rand_bit(densProb);
			gates <<= 1;
		}

		slides = 0;
		for(int i=0; i<ACID_MAX_STEPS; ++i)
		{
			  // Less probability of consecutive slides
			latest = rand_bit((latest ? 10 : 14));
			slides |= latest;
			slides <<= 1;
		}

		accents = 0;
		for(int i=0; i<ACID_MAX_STEPS; ++i)
		{
			// Less probability of consecutive accents
			latest = rand_bit((latest ? 5 : 10));
			accents |= latest;
			accents <<= 1;
		}

		
	}
	

    bool step_is_gated(int step) {
        return (gates & (0x01 << step));
    }
	
    bool step_is_slid(int step) {
        return (slides & (0x01 << step));
    }

    bool step_is_accent(int step) {
        return (accents & (0x01 << step));
    }

	int get_next_step(int step)
	{
		if(++step == ACID_MAX_STEPS)
		{
			return 0;
		}
		return step;  // Advanced
	}


	

	int rand_bit(int prob)
	{
		return (random(1, 100) <= prob) ? 1 : 0;
	}

  void DrawSelector()
  {
     
    // Draw settings
    gfxPrint(1, 15, OC::scale_names_short[scale]);
    //gfxBitmap(31, 25, 8, notes);
    gfxPrint(41, 15, OC::Strings::note_names_unpadded[root]);
    
    gfxPrint(1, 25, "dens:"); gfxPrint(density);


    //gfxPrint(1, 35, "gt:");gfxPrint(gates);  // DEBUG
    // Status
    //gfxPrint(51, 45, step);
    gfxPrint(1 + pad(100,step), 45, step); gfxPrint("/");gfxPrint(ACID_MAX_STEPS);  // Pad x enough to hold width steady
    gfxPrint(1, 55, "Note:"); gfxPrint(notes[step]);
    
    // Draw cursor
    
    
    if (cursor == 0) 
    {
      gfxCursor(1, 23, 30);
    }
    else if(cursor == 1)
    {
      gfxCursor(41, 23, 12);
    }
    else if(cursor == 2)
    {
      gfxCursor(33, 33, 30);
    }
             
  }




	/*
    void DrawPanel() {
		
		
        // Sliders
        for (int s = 0; s < SEQ5_STEPS; s++)
        {
            int x = 6 + (12 * s);
            //int x = 6 + (7 * s); // APD:  narrower to fit more
            
            if (!step_is_muted(s)) {
                gfxLine(x, 25, x, 63);

                // When cursor, there's a heavier bar and a solid slider
                if (s == cursor) {
                    gfxLine(x + 1, 25, x + 1, 63);
                    gfxRect(x - 4, BottomAlign(note[s]), 9, 3);
                    //gfxRect(x - 2, BottomAlign(note[s]), 5, 3);  // APD
                } else 
                {
                  gfxFrame(x - 4, BottomAlign(note[s]), 9, 3);
                  //gfxFrame(x - 2, BottomAlign(note[s]), 5, 3);  // APD
                }
                
                // When on this step, there's an indicator circle
                if (s == step) 
                {

                  
                  gfxCircle(x, 20, 3);  //Original

                  // APD
                  //int play_note = note[step];// + 60 + transpose;

                  //gfxPrint(10, 15, "Scale ");
                  //gfxPrint(cursor < 12 ? 1 : 2);
                  //gfxPrint(x, 20, play_note);
                  
                }
            } else if (s == cursor) {
                gfxLine(x, 25, x, 63);
                gfxLine(x + 1, 25, x + 1, 63);
             }
        }
    }
	*/
	

};


////////////////////////////////////////////////////////////////////////////////
//// Hemisphere Applet Functions
///
///  Once you run the find-and-replace to make these refer to TB_3PO,
///  it's usually not necessary to do anything with these functions. You
///  should prefer to handle things in the HemisphereApplet child class
///  above.
////////////////////////////////////////////////////////////////////////////////
TB_3PO TB_3PO_instance[2];

void TB_3PO_Start(bool hemisphere) {
    TB_3PO_instance[hemisphere].BaseStart(hemisphere);
}

void TB_3PO_Controller(bool hemisphere, bool forwarding) {
    TB_3PO_instance[hemisphere].BaseController(forwarding);
}

void TB_3PO_View(bool hemisphere) {
    TB_3PO_instance[hemisphere].BaseView();
}

void TB_3PO_OnButtonPress(bool hemisphere) {
    TB_3PO_instance[hemisphere].OnButtonPress();
}

void TB_3PO_OnEncoderMove(bool hemisphere, int direction) {
    TB_3PO_instance[hemisphere].OnEncoderMove(direction);
}

void TB_3PO_ToggleHelpScreen(bool hemisphere) {
    TB_3PO_instance[hemisphere].HelpScreen();
}

uint32_t TB_3PO_OnDataRequest(bool hemisphere) {
    return TB_3PO_instance[hemisphere].OnDataRequest();
}

void TB_3PO_OnDataReceive(bool hemisphere, uint32_t data) {
    TB_3PO_instance[hemisphere].OnDataReceive(data);
}
