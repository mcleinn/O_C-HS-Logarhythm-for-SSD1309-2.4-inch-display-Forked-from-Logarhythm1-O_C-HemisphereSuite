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

//#include "HSMIDI.h"
#include "braids_quantizer.h"
#include "braids_quantizer_scales.h"
#include "OC_scales.h"


#define ACID_MAX_STEPS 16


class TB_3PO : public HemisphereApplet {
public:

    const uint8_t RANDOM_ICON[8] = {0x7c,0x82,0x8a,0x82,0xa2,0x82,0x7c,0x00};  // A die showing '2'

    const char* applet_name() { // Maximum 10 characters
        return "TB-3PO";
    }

    void Start() 
    {
    	
    	seed = random(0, 65535); // 16 bits
    	regenerate(seed);
      
      scale = 29;  // GUNA scale sounds cool   //OC::Scales::SCALE_SEMI; // semi sounds pretty bunk
      quantizer.Init();
      quantizer.Configure(OC::Scales::GetScale(scale), 0xffff);

      //timing_count = 0;
      gate_off_clock = 0;
      cycle_time = 0;

      curr_gate_cv = 0;


      curr_pitch_cv = 0;
      slide_end_cv = 0;
      
      transpose_note_in = 0;    
      
    	play = 1;
    }

    void Controller() 
    {
      // Track timing to set gate timing at ~32nd notes per recent clocks
      int this_tick = OC::CORE::ticks;
      
      // Regenerate / Reset
      if (Clock(1)) 
      {

        //timing_count = 0;
        
        // TODO: debounce this with some kind of delay
        seed = random(0, 65535); // 16 bits
        regenerate(seed);
        //step = 0;
        //ClockOut(1);
      }
      
      transpose_note_in = 0;
      if (DetentedIn(0)) 
      {
        transpose_note_in = In(0) / 128; // 128 ADC steps per semitone
      }

      
      //int play_note = notes[step] + 60 + transpose;
      //play_note = constrain(play_note, 0, 127);
      
      // Wait for the ADC since transpose CV is needed
      if (Clock(0)) 
      {
        cycle_time = ClockCycleTicks(0);  // Track latest interval of clock 0 for gate timings
        
        StartADCLag();
      }

      if (EndOfADCLag() && !Gate(1))  // Reset not held
      {
        int step_pv = step;
        
        // Advance the step
        step = get_next_step(step);


        // Was step before this one set to 'slide'?
        // If so, engage a slide from its pitch to this step's pitch now
        if(step_is_slid(step_pv))
        {

          // TRIGGER SLIDE to own pitch
          int slide_start_cv = get_pitch_for_step(step_pv);


          // Jump current pitch to prior step's value if not there already
          // TODO: Consider just gliding from whereever it is?
          curr_pitch_cv = slide_start_cv;
  
          // Slide target pitch
          slide_end_cv = get_pitch_for_step(step);

        }
        else
        {
          // Prior step was not slid, so snap to current pitch
          curr_pitch_cv = get_pitch_for_step(step);
          slide_end_cv = curr_pitch_cv;
        }


        
        
        // Open the gate if this step is gated, or hold it open for at least 1/2 step if the prior step was slid
        if(step_is_gated(step) || step_is_slid(step_pv))
        {
          //ClockOut(1);
  
          // Accented gates get a higher voltage, so it can drive VCA gain in addition to triggering envelope generators
          curr_gate_cv = step_is_accent(step) ? HEMISPHERE_MAX_CV : HEMISPHERE_3V_CV;

          // Set up the timing for this gate
          //cycle_time = ClockCycleTicks(0);  // Track for gate times
          //++timing_count;
          // On each clock, schedule the next clock at a multiplied rate
          int gate_time = (cycle_time / 2);  // multiplier of 2
          gate_off_clock = this_tick + gate_time;
        }
 
        //play = 1;
      }

      // Update the clock multiplier for gate timings
      //if(curr_gate_cv > 0 && this_tick >= next_clock)
      if(curr_gate_cv > 0 && gate_off_clock > 0 && this_tick >= gate_off_clock)
      {
        // Time for gate off
        
        gate_off_clock = 0;
        //int clock_every = (cycle_time / 2);  // Use half of latest measured clock cycle time for gate lengths
        //next_clock += clock_every;
        
        // Do nothing if the current step should be slid
        if(!step_is_slid(step))
        {
          curr_gate_cv = 0;//HEMISPHERE_CENTER_CV;
        }
      }

      
      // TODO: Apply CV density directly to regenerate gates, if int change?
      // TODO: Maybe instead, this cv live-controls a gate skip % chance? (And also sets density when regenerating)
      
      
      //if (play) 
      //{
   
        // Note: If prior step was slid, the gate should still be open, and this pitch change should be slid
        // Time to bend?
        // Start cv is 

        // From Sequins APP_A_SEQ:
        //step_pitch_ = get_pitch_at_step(display_num_sequence_, clk_cnt_) + (_octave * 12 << 7); 
    
        // TODO: Set a target pitch instead for slides, with expo 
        //int cv = pitches[step]; //MIDIQuantizer::CV(play_note);
        
        //int quant_note = notes[step] + 64 + root + transpose_note_in;
        //curr_pitch_cv = quantizer.Lookup( constrain(quant_note, 0, 127));
        // curr_glide_cv
        //curr_pitch_cv = get_pitch_for_step(step);
        
        // TODO: Move this out of clock only when gliding
        //Out(0, curr_pitch_cv);
      //}


      // Update slide if needed
      if(curr_pitch_cv != slide_end_cv)
      {

        // Working: This gives constant rate linear glide (but we want expo fixed-time)
        curr_pitch_cv +=  (slide_end_cv - curr_pitch_cv > 0 ? 1 : -1);

        
        /*  this isn't working right
        int slide_const = 3000;
        
        curr_pitch_cv +=  simfloat2int((int2simfloat( (slide_end_cv - curr_pitch_cv) )) / (int2simfloat(slide_const)));
        */
        //curr_pitch_cv = Proportion(curr_step, steps, HEMISPHERE_MAX_CV);  // 0-5v, scaled with fixed-point


        int epsilon = 1;  // Figure out what this should be to snap to 100% pitch as approach is near enough
        if(curr_pitch_cv + epsilon > slide_end_cv)
          curr_pitch_cv = slide_end_cv;
      }


      // Pitch out
      Out(0, curr_pitch_cv);
      
      // Gate out (as CV)
      Out(1, curr_gate_cv);
      
    }

    void View() {
      gfxHeader(applet_name());
      DrawSelector();
    }

    void OnButtonPress() {
      if (++cursor > 4) cursor = 0;
    }

    void OnEncoderMove(int direction) 
    {
      if (cursor == 0)
      {
        // seed
      }
      else if (cursor == 1)
      {
        // Scale selection
        scale += direction;
        if (scale >= OC::Scales::NUM_SCALES) scale = 0;
        if (scale < 0) scale = OC::Scales::NUM_SCALES - 1;
        quantizer.Configure(OC::Scales::GetScale(scale), 0xffff);

        //continuous[ch] = 1; // Re-enable continuous mode when scale is changed
      } 
      else if(cursor == 2)
      {
        // Root selection
        root = constrain(root + direction, 0, 11);
      }
      else if(cursor == 3)
      {
      	// Set for the next time a pattern is generated
      	density = constrain(density + direction, 0, 10);
      	
      	
      	// TODO: Maybe instead of altering the existing gates, just set a mask to logical-AND against the existing pattern?
      	// Apply the new density live
      	//apply_density();  // TODO: Move to update loop to react to cv as well?
      }
      else
      {
        num_steps = constrain(num_steps + direction, 1, 16);
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

      quantizer.Configure(OC::Scales::GetScale(scale), 0xffff);
      
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
  int num_steps = 16;
  int notes[ACID_MAX_STEPS];
  //int pitches[ACID_MAX_STEPS];  // Use cv since we get the available ones from the quantizer

  int transpose_note_in;  // Current transposition, in note numbers
  
  int step = 0; // Current sequencer step
  bool play; // Play the note
  
  // For gate timing as ~32nd notes at tempo, detect clock rate like a clock multiplier
  //int timing_count;
  int gate_off_clock;
  int cycle_time; // Cycle time between the last two clock inputs
  
  int curr_gate_cv = 0;

  //int slide_start_cv = 0;
  int curr_pitch_cv = 0;
  int slide_end_cv = 0;
  //int slide_ticks = 0;

  braids::Quantizer quantizer;  // Helper for note index --> pitch cv
  
  uint16_t seed;
  int scale;		// Scale
  uint8_t root = 0;	// Root note
  
  uint8_t density = 10;  // 


  // Get the cv value to use for a given step including root + transpose values
  int get_pitch_for_step(int step_num)
  {
    int quant_note = notes[step_num] + 64 + root + transpose_note_in;
    return quantizer.Lookup( constrain(quant_note, 0, 127));
  }
  
	
	void regenerate(int seed)
	{
		randomSeed(seed);  // Ensure random()'s seed
		
		regenerate_pitches();
		apply_density();
	}
	
  // Regenerate pitches
  void regenerate_pitches()
  {
    //int bag[4] = {0, 12, 1, 7};

    // Get the available note count to choose from per oct
    // This doesn't really matter since notes are index-based, and the quant scale can be changed live
    // But it will color the random note selection to the scale maybe?
    const braids::Scale & quant_scale = OC::Scales::GetScale(scale);
    int num_notes = quant_scale.num_notes;

    
    for (int s = 0; s < ACID_MAX_STEPS; s++) 
    {
      /*
      // Test octs
    	//notes[s] = root + rand_bit(40) * 12; //random(0, 30);
      notes[s] = root;
      if(s == ACID_MAX_STEPS-1)
        notes[s]+=12;
      */


      /*
      if(s > 0)
      {
        // Chance to hold prior note
        if(rand_bit(10))
        {
          notes[s] = notes[s-1];
        }
      }
      else
      {
      */
        //notes[s] = root + bag[random(0,3)];
        
        // Grab a note from the scale
        notes[s] = random(0,num_notes-1);

        // Random oct up or down (Treating octave based on the scale's number of notes)
        if(rand_bit(40))
        {
          notes[s] += num_notes * (rand_bit(50) ? -1 : 1);
        }
        
      //}
 
      
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

    // TEST SLIDES
    //slides = 0x1111;
    
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
		if(++step >= num_steps)//ACID_MAX_STEPS)
		{
			return 0;
		}
		return step;  // Advanced by one
	}
	
  // Pass in a probability 0-100 to get that % chance to return 1
	int rand_bit(int prob)
	{
		return (random(1, 100) <= prob) ? 1 : 0;
	}

  void DrawSelector()
  {
     
    // Draw settings
    
    gfxBitmap(1, 15, 8, RANDOM_ICON);
    gfxPrint(12, 15, " ");
    // Show seed in hex (todo: editable, move to first item?)
    int disp_seed = seed;
    //gfxPrint(1, 15, "seed: ");
    char sz[2]; sz[1] = 0;  // Null terminated string
    for(int i=0; i<4; ++i)
    {
      int nib = disp_seed & 0xF;
      if(nib<=9)
      {
        gfxPrint(nib);
      }
      else
      {
        sz[0] = 'a' + nib - 10;
        gfxPrint(static_cast<const char*>(sz));
      }
      //char c = nib <= 9 ? '0'+nib : 'a'+nib-10;  // hex char
      //gfxPrint(c);
      disp_seed >>= 4;
    }



    
    gfxPrint(1, 25, OC::scale_names_short[scale]);
    //gfxBitmap(31, 25, 8, notes);
    gfxPrint(41, 25, OC::Strings::note_names_unpadded[root]);
    
    //gfxPrint(1, 35, "dens:"); gfxPrint(density);
    
    gfxBitmap(1, 35, 8, NOTE4_ICON);gfxBitmap(6, 35, 8, NOTE4_ICON);  // Jam a couple of these together
    gfxPrint(12+8, 35, density);

    //gfxPrint(1, 35, "gt:");gfxPrint(gates);  // DEBUG

   // Display 16-bit seed in 4 hex digits -- allow per-character editing for user recall?  (Use hand-entered seed on next "regenerate" rather than re-rolling it)



    
    int display_step = step+1;  // Protocol droids know that humans count from 1
    gfxPrint(1 + pad(100,display_step), 45, display_step); gfxPrint("/");gfxPrint(num_steps);  // Pad x enough to hold width steady
    //gfxPrint(1, 55, "pitch: "); gfxPrint(notes[step]);

    
    gfxPrint(50, 55, notes[step]);
    //gfxBitmap(1, 55, 8, CV_ICON); gfxPos(12, 55); gfxPrintVoltage(pitches[step]);

    // SEE gfxIcon and gfxBitmap possibilities

    int iPlayingIndex = (root + notes[step]) % 12;  // TODO: use current scale modulo
    // Draw notes

    
    //Test
    //gfxFrame(3, 59, 5, 4);
    //gfxFrame(3+6, 59, 5, 4);

    
    // TODO: Use current scale num notes
    int x = 3;
    int y = 59;
    int keyPatt = 0x054A; // keys encoded as 0=white 1=black, starting at c, backwards:  b  0 0101 0100 1010
    for(int i=0; i<12; ++i)
    {
      // Black key?
      y = ( keyPatt & 0x1 ) ? 54 : 59;
      keyPatt >>= 1;
      
      // Two white keys in a row E and F
      if( i == 5 ) x+=3;

      if(iPlayingIndex == i && step_is_gated(step))  // Only render a pitch if gated
      {
        gfxRect(x, y, 5, 4);
      }
      else
      {
        gfxFrame(x, y, 5, 4);
      }
      
      x += 3;
    }

    
    // Indicate slide circuit activate
    //int prior_step = step-1; if(step<0) step = num_steps-1;
    //if(step_is_slid(prior_step))
    if(step_is_slid(step))
    {
        gfxBitmap(42, 46, 8, BEND_ICON);
    }

    // running slide
    if(curr_pitch_cv != slide_end_cv)
    {
      //gfxBitmap(50, 59, 8, BEND_ICON);
      gfxBitmap(52, 46, 8, WAVEFORM_ICON);
    }
    
    // Draw cursor
    if (cursor == 0) // seed
    {
      gfxCursor(1, 23, 30);
    }
    else if(cursor == 1)
    {
      gfxCursor(1, 33, 30);
    }
    else if(cursor == 2)
    {
      gfxCursor(41, 33, 12);
    }
    else if(cursor == 3)
    {
      gfxCursor(33, 43, 30);
    }
    else if(cursor == 4)
    {
      gfxCursor(25, 55, 16);
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
