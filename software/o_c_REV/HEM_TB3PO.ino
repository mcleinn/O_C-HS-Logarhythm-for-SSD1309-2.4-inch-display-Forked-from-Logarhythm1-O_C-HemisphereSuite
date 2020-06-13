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
// CV pitch out includes fixed-time exponential pitch slides timed as on 303s
// CV gates are output at 3v for normal notes and 5v for accented notes
// TODO: Output proper envelopes?


#include "braids_quantizer.h"
#include "braids_quantizer_scales.h"
#include "OC_scales.h"

#define ACID_MAX_STEPS 16

class TB_3PO : public HemisphereApplet 
{
  public:

    const uint8_t RANDOM_ICON[8] = {0x7c,0x82,0x8a,0x82,0xa2,0x82,0x7c,0x00};  // A die showing '2'

    const char* applet_name() { // Maximum 10 characters
        return "TB-3PO";
    }

    void Start() 
    {
      lock_seed = 0;
    	seed = random(0, 65535); // 16 bits
    	regenerate(seed);

      manual_reset_flag = 0;
      rand_apply_anim = 0;
      curr_step_semitone = 0;
      
      root = 0;
      // Init the quantizer for selecting pitches / CVs from
      scale = 29;  // GUNA scale sounds cool   //OC::Scales::SCALE_SEMI; // semi sounds pretty bunk
      quantizer.Init();
      set_quantizer_scale(scale);

      // This quantizer is for displaying a keyboard graphic, mapping the current scale to semitones
      display_semi_quantizer.Init();
      display_semi_quantizer.Configure(OC::Scales::GetScale(OC::Scales::SCALE_SEMI), 0xffff);
      
      density = 12;
      density_cv_lock = 0;

      num_steps = 16;
      
      gate_off_clock = 0;
      cycle_time = 0;

      curr_gate_cv = 0;
      curr_pitch_cv = 0;
      
      slide_start_cv = 0;
      slide_end_cv = 0;
      
      transpose_note_in = 0;    
    }

    void Controller() 
    {
      // Track timing to set gate timing at ~32nd notes per recent clocks
      int this_tick = OC::CORE::ticks;
      
      // Regenerate / Reset
      if (Clock(1) || manual_reset_flag) 
      {
        manual_reset_flag = 0;
        // If the seed is not locked, then randomize it on every reset pulse
        // Otherwise, the user has locked it, so leave it as set
        if(lock_seed == 0)
        {
          seed = random(0, 65535); // 16 bits
        }

        // Apply the seed to regenerate the pattern`
        // This is deterministic so if the seed is held, the pattern will not change
        regenerate(seed);

        // Reset step
        step = 0;
      }


      // Control transpose from cv1 (Very fun to wiggle)
      transpose_note_in = 0;
      if (DetentedIn(0)) 
      {
        transpose_note_in = In(0) / 128; // 128 ADC steps per semitone
      }

      // Control density from cv1 (Wiggling can build up & break down patterns nicely, especially if seed is locked)
      density_cv_lock = 0;  // Track if density is under cv control
      if (DetentedIn(1)) 
      {
          int num = ProportionCV(In(1), 15);
          density = constrain(num, 0, 14);
          density_cv_lock = 1;
      }

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
        // If so, engage a the 'slide circuit' from its pitch to this new step's pitch
        if(step_is_slid(step_pv))
        {
          // Slide begins from the prior step's pitch (TODO: just use current dac output?)
          slide_start_cv = get_pitch_for_step(step_pv);

          // Jump current pitch to prior step's value if not there already
          // TODO: Consider just gliding from whereever it is?
          curr_pitch_cv = slide_start_cv;
  
          // Slide target is this step's pitch
          slide_end_cv = get_pitch_for_step(step);
        }
        else
        {
          // Prior step was not slid, so snap to current pitch
          curr_pitch_cv = get_pitch_for_step(step);
          slide_start_cv = curr_pitch_cv;
          slide_end_cv = curr_pitch_cv;
        }
  
        // Open the gate if this step is gated, or hold it open for at least 1/2 step if the prior step was slid
        if(step_is_gated(step) || step_is_slid(step_pv))
        {
          // Accented gates get a higher voltage, so it can drive VCA gain in addition to triggering envelope generators
          curr_gate_cv = step_is_accent(step) ? HEMISPHERE_MAX_CV : HEMISPHERE_3V_CV;

          // On each clock, schedule the next clock at a multiplied rate
          int gate_time = (cycle_time / 2);  // multiplier of 2
          gate_off_clock = this_tick + gate_time;
        }

        // When changing steps, compute the nearest semitone at the base octave to show on the keyboard
        curr_step_semitone = get_semitone_for_step(step);
        
      }

      // Update the clock multiplier for gate off timings
      if(curr_gate_cv > 0 && gate_off_clock > 0 && this_tick >= gate_off_clock)
      {
        // Handle turning the gate off, unless sliding
        gate_off_clock = 0;
        
        // Do nothing if the current step should be slid
        if(!step_is_slid(step))
        {
          curr_gate_cv = 0;//HEMISPHERE_CENTER_CV;
        }
      }
      
      // TODO: Apply CV density directly to regenerate gates, if int change?
      // TODO: Maybe instead, this cv live-controls a gate skip % chance? (And also sets density when regenerating)

      // Update slide if needed
      if(curr_pitch_cv != slide_end_cv)
      {
        // This gives constant rate linear glide (but we want expo fixed-time):
        // curr_pitch_cv +=  (slide_end_cv - curr_pitch_cv > 0 ? 1 : -1);

        // (This could optionally use peak's lut_env_expo[] for interpolation instead)
        // Expo slide (code assist from CBS)
        int k = 0x0003;  // expo constant:  0 = infinite time to settle, 0xFFFF ~= 1, fastest rate
                        // Choose this to give 303-like pitch slide timings given the O&C's update rate
        // k = 0x3 sounds good here with >>=18    
        
        int x = slide_end_cv;
        x -= curr_pitch_cv;
        x >>= 18;  
        x *= k;
        curr_pitch_cv += x;

        // TODO: Check constrain
        if(slide_start_cv < slide_end_cv)
        {
          curr_pitch_cv = constrain(curr_pitch_cv, slide_start_cv, slide_end_cv);

          // set a bit if constrain was needed
        }
        else
        {
          curr_pitch_cv = constrain(curr_pitch_cv, slide_end_cv, slide_start_cv);

          // set a bit if constrain was needed
        }
      }

      // Pitch out
      Out(0, curr_pitch_cv);
      
      // Gate out (as CV)
      Out(1, curr_gate_cv);


      // Timesliced generation of new patterns, if triggered
      // Do this last to not interfere with the body of the time for this hemisphere's update
      // (This is speculation without knowing how to best profile performance on this system)
      update_regeneration();
     
    }

    void View() {
      gfxHeader(applet_name());
      DrawGraphics();
    }

    void OnButtonPress() 
    {
      if(cursor == 0)
      {
        cursor = lock_seed ? 1 : 5;
      }
      else if (++cursor > 8) 
      {
        cursor = 0;
      }
      
      ResetCursor();  // Reset blink so it's immediately visible when moved
    }

    void OnEncoderMove(int direction) 
    {
      if(cursor == 0)
      {
        // Toggle the seed between auto (randomized every reset input pulse) 
        // or Manual (seed becomes locked, cursor can be moved to edit each digit)
        lock_seed += direction;

        // See if the turn would move beyond the random die to the left or the lock to the right
        // If so, take this as a manual input just like receiving a reset pulse (handled in Controller())
        // regenerate() will honor the random or locked icon shown (seed will be randomized or not)
        manual_reset_flag = (lock_seed > 1 || lock_seed < 0) ? 1 : 0;
        
        // constrain to legal values before regeneration
        lock_seed = constrain(lock_seed, 0, 1);
      }
      else if (cursor <= 4)
      {
        // Editing one of the 4 hex digits of the seed
        
        // cursor==1 is at the most significant byte, 
        // cursor==4 is at least significant byte
        int byte_offs = 4-cursor;  
        int shift_amt = byte_offs*4;

        uint32_t nib = (seed >> shift_amt)& 0xf; // Abduct the nibble
        uint8_t c = nib;
        c = constrain(c+direction, 0, 0xF);  // Edit the nibble
        nib = c;
        uint32_t mask = 0xf;
        seed &= ~(mask << shift_amt);  // Clear bits where this nibble lives
        seed |= (nib << shift_amt);    // Move the nibble to its home
      }
      else if (cursor == 5)
      {
        // Set for the next time a pattern is generated
        //density = constrain(density + direction, 0, 12);
        density = constrain(density + direction, 0, 14);  // Treated as a bipolar -7 to 7 in practice
        
        // TODO: Maybe instead of altering the existing gates, just set a mask to logical-AND against the existing pattern?
        // Apply the new density live
        //apply_density();  // TODO: Move to update loop to react to cv as well?
      } 
      else if(cursor == 6)
      {
        // Scale selection
        scale += direction;
        if (scale >= OC::Scales::NUM_SCALES) scale = 0;
        if (scale < 0) scale = OC::Scales::NUM_SCALES - 1;
        //quantizer.Configure(OC::Scales::GetScale(scale), 0xffff);
        //const braids::Scale & quant_scale = OC::Scales::GetScale(scale);
        //quantizer.Configure(quant_scale, 0xffff);
        //scale_size = quant_scale.num_notes;  // Track this scale size for display
        set_quantizer_scale(scale);

      }
      else if(cursor == 7)
      {
        // Root note selection
        root = constrain(root + direction, 0, 11);
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

      //const braids::Scale & quant_scale = OC::Scales::GetScale(scale);
      set_quantizer_scale(scale);
      
      //scale = constrain(0, OC::Scales::NUM_SCALES-1);
      root = constrain(root, 0, 11);
      density = constrain(density, 0, 14); // Internally just positive
      
      // Restore all seed-derived settings!
      regenerate(seed);

      // Reset step
      step = 0;

    }

  protected:
    void SetHelp() {
        //                               "------------------" <-- Size Guide
        help[HEMISPHERE_HELP_DIGITALS] = "1=Clock 2=Regen";
        help[HEMISPHERE_HELP_CVS]      = "1=Transp 2=Density";
        help[HEMISPHERE_HELP_OUTS]     = "A=CV+glide B=Gate";
        help[HEMISPHERE_HELP_ENCODER]  = "seed/qnt/dens/len";
        //                               "------------------" <-- Size Guide
    }
    
  private:
    int cursor = 0;

    braids::Quantizer quantizer;  // Helper for note index --> pitch cv
    braids::Quantizer display_semi_quantizer;  // Quantizer to interpret the current note for display on a keyboard
    
  
    // User settings
    int manual_reset_flag = 0;  // Manual trigger to reset/regen

    int lock_seed;  // If 1, the seed won't randomize (and manual editing is enabled)
    uint16_t seed;  // The random seed that deterministically builds the sequence
    
    int scale;      // Active quantization & generation scale
    uint8_t root;   // Root note

    uint8_t density;  // The density parameter controls a couple of things at once. Its 0-14 value is mapped to -7..+7 range
                      // The larger the magnitude from zero in either direction, the more dense the note patterns are (fewer rests)
                      // For values mapped < 0 (e.g. left range,) the more negative the value is, the less chance consecutive pitches will
                      // change from the prior pitch, giving repeating lines (note: octave jumps still apply)

    int density_cv_lock;  // Tracks if density is under cv control (can't change manually)

    int num_steps;        // How many steps of the generated pattern to play before looping
    
    // Playback
    int step = 0;           // Current sequencer step
    int transpose_note_in;  // Current transposition, in note numbers

    // Generated sequence data
    int gates = 0; 		// Bitfield of gates;  ((gates >> step) & 1) means gate
    int slides = 0; 	// Bitfield of slide steps; ((slides >> step) & 1) means slide
    int accents = 0; 	// Bitfield of accent steps; ((accents >> step) & 1) means accent
    int notes[ACID_MAX_STEPS];  // Note values

    int scale_size;  // The size of the scale used to generate the pattern (for octave detection, etc)
    
    // For gate timing as ~32nd notes at tempo, detect clock rate like a clock multiplier
    //int timing_count;
    int gate_off_clock; // Scheduled cycle at which the gate should be turned off (when applicable)
    int cycle_time;     // Cycle time between the last two clock inputs

    // CV output values
    int curr_gate_cv = 0;
    int curr_pitch_cv = 0;

    // Pitch slide cv tracking
    int slide_start_cv = 0;
    int slide_end_cv = 0;

    // Display
    int curr_step_semitone = 0;  // The pitch converted to nearest semitone, for showing as an index onto the keyboard
    
    int rand_apply_anim = 0;  // Countdown to animate icons for when regenerate occurs

    int regenerate_phase = 0;  // Split up random generation over multiple frames
  
    // Get the cv value to use for a given step including root + transpose values
    int get_pitch_for_step(int step_num)
    {
      //int quant_note = notes[step_num] + 64 + root + transpose_note_in;
      //int base_note_for_scale = 64;  // 64 should be 0v baseline
      
      int quant_note = 64 + notes[step_num] +  root + transpose_note_in;

      int out_note = constrain(quant_note, 0, 127);
      return quantizer.Lookup( out_note );
      //return quantizer.Lookup( 64 + notes[step_num]+root);
      //return quantizer.Lookup( 64 );  // note 64 is definitely 0v=c4 if output directly, on ALL scales
    }

    int get_semitone_for_step(int step_num)
    {
/*
      //int quant_note = notes[step_num] + 64 + root + transpose_note_in;
      int base_note_for_scale = 64;  // 64 should be 0v baseline
      int quant_note = notes[step_num] + base_note_for_scale + root + transpose_note_in;  // c is base transpose, so use its midi note 60
      //int quant_note = (notes[step_num] % scale_size) + 64 + root + transpose_note_in;
      
      //int32_t cv_single_oct = quantizer.Lookup( constrain(quant_note, 0, 127));
      // Now subject the octave-constained cv to quantization to semitones to map approximately onto a piano keyboard
      // Note: retval is unneeded because the note number it picks will be queried here
      //display_semi_quantizer.Process(cv_single_oct, 0, 0);  // Use root == 0 to start at c


      int32_t cv_note = quantizer.Lookup( constrain(quant_note, 0, 127));
      display_semi_quantizer.Process(cv_note, 0, 0);  // Use root == 0 to start at c
      
      // Note: This accessor for the quantizer's latest note number must be added to braids_quantizer.h:
      //uint16_t GetLatestNoteNumber() { return note_number_;}
      //return (display_semi_quantizer.GetLatestNoteNumber() - 4) % 12;  // N.B. Move by 4 to move down to 'c' on display for 0 xpose  TODO: Figure out what voltages are going out!
      return display_semi_quantizer.GetLatestNoteNumber() % 12;
      */

      return notes[step_num] + root + transpose_note_in;
    }

    
  	// Generate the sequence deterministically using the seed
  	void regenerate(int seed)
  	{
		  //randomSeed(seed);  // Ensure random()'s seed 
      
      //regenerate_pitches();
  		//apply_density();
      regenerate_phase = 1;  // Set to regenerate on loop
      
      rand_apply_anim = 40;  // Show that regenerate occured (anim for this many display updates)
  	}


    // Amortize random generation over multiple frames
    // Without having profiled this properly, I'm less concerned about overrunning isr times alloted to this app if it's amortized
    void update_regeneration()
    {
      if(regenerate_phase == 0)
      {
        return;
      }
      
      randomSeed(seed+regenerate_phase);  // Ensure random()'s seed at each phase for determinism (note: offset to decouple phase behavior correllations that would result)
      
      switch(regenerate_phase)
      {
        case 1: regenerate_pitches(); ++regenerate_phase; break;
        case 2: apply_density(); regenerate_phase = 0;break;
        default: break;
      }
    }

    
    
    // Generate the notes sequence based on the seed and modified by density
    void regenerate_pitches()
    {
      // Get the available note count to choose from per oct
      // This doesn't really matter since notes are index-based, and the quant scale can be changed live
      // But it will color the random note selection to the scale maybe?
      //const braids::Scale & quant_scale = OC::Scales::GetScale(scale);
      //scale_size = quant_scale.num_notes;  // Track this scale size for display

      // How much pitch variety to use from the available pitches (one of the factors of the 'density' control when < centerpoint)
      int pitch_change_dens = get_pitch_change_density();   
      int available_pitches = 0;
      if(scale_size > 0)
      {
        if(pitch_change_dens > 7)
        {
          available_pitches = scale_size-1;
        }
        else if(pitch_change_dens < 2)
        {
           // Give the behavior of just the root note (0) at lowest density, and 0&1 at 2nd lowest (for 303 half-step style)
          available_pitches = pitch_change_dens;
        }
        else  // Range 3-7
        {
          int range_from_scale = scale_size - 3;
          if(range_from_scale < 4)  // Ok to saturate at full note count
          {
            range_from_scale = 4;
          }
          // Range from 2 pitches to just <= full scale available
          available_pitches = 3 + Proportion(pitch_change_dens-3, 4, range_from_scale);
          available_pitches = constrain(available_pitches, 1, scale_size -1);
        }
      }

      for (int s = 0; s < ACID_MAX_STEPS; s++) 
      {
        // Increased chance to repeat the prior note, the smaller the pitch change aspect of 'density' is
        // 0-8, least to most likely to change pitch
        int force_repeat_note_prob = 50 - (pitch_change_dens * 6);
        if(s > 0 && rand_bit(force_repeat_note_prob))
        {
          notes[s] = notes[s-1];
        }
        else
        {
          /*
          if(available_pitches <= 1)
          {
            notes[s] = 0;
          }
          else*/
          {
            // Grab a random note index from the scale's available pitches
            // Since this starts at 0, the root note will always be included, and adjacent scale notes are included as the range grows
            notes[s] = random(0,available_pitches+1);  // Looking at the source, random(min,max) appears to return the range: min to max-1
          }
          // Random oct up or down (Treating octave based on the scale's number of notes)
          if(rand_bit(40))
          {
            // Use 12-note octave if 'none'
            notes[s] += (scale_size > 0 ? scale_size : 12) * (rand_bit(50) ? -1 : 1);
          }
        }      
      }

      // Handle size as semitone scale for display if 'off'
      if(scale_size == 0)
      {
        scale_size = 12;
      }
  	}
  	
  	// Change pattern density without affecting pitches
  	void apply_density()
  	{
  		int latest = 0; // Track previous bit for some algos
  		
  		gates = 0;
      // Get gate probability from the 'density' value
      int on_off_dens = get_on_off_density();
      int densProb = 10 + on_off_dens * 14;  // Should start >0 and reach 100+
  		for(int i=0; i<ACID_MAX_STEPS; ++i)
  		{
  			gates |= rand_bit(densProb);
  			gates <<= 1;
  		}
  
  		slides = 0;
  		for(int i=0; i<ACID_MAX_STEPS; ++i)
  		{
  		  // Less probability of consecutive slides
  			latest = rand_bit((latest ? 10 : 18));
  			slides |= latest;
  			slides <<= 1;
  		}
  
  		accents = 0;
  		for(int i=0; i<ACID_MAX_STEPS; ++i)
  		{
  			// Less probability of consecutive accents
  			latest = rand_bit((latest ? 7 : 16));
  			accents |= latest;
  			accents <<= 1;
  		}
  	}

    // Get on/off likelihood from the current value of 'density'
    int get_on_off_density()
    {
      // density has a range 0-14
      // Convert density to a bipolar value from -7..+7, with the +-7 extremes in either direction 
      // as high note density, and the 0 point as lowest possible note density
      int note_dens = int(density) - 7;
      return abs(note_dens);
    }

    // Get the degree to which pitches should change based on the value of 'density'
    // The density slider's center and right half indicate full pitch change range
    // The further the slider is to the left of the centerpoint, the less pitches should change
    int get_pitch_change_density()
    {
      // Smaller values indicate fewer pitches should be drawn from
      return constrain(density, 0,8);  // Note that the right half of the slider is clamped to full range
    }
 
    bool step_is_gated(int step_num) {
        return (gates & (0x01 << step_num));
    }
    
    bool step_is_slid(int step_num) {
        return (slides & (0x01 << step_num));
    }
    
    bool step_is_accent(int step_num) {
        return (accents & (0x01 << step_num));
    }
  
  	int get_next_step(int step_num)
  	{
      // loop at the current loop point
  		if(++step_num >= num_steps)
  		{
  			return 0;
  		}
  		return step_num;  // Advanced by one
  	}

    // Pass in a probability 0-100 to get that % chance to return 1
  	int rand_bit(int prob)
  	{
  		return (random(1, 100) <= prob) ? 1 : 0;
  	}

    void set_quantizer_scale(int new_scale)
    {
      const braids::Scale & quant_scale = OC::Scales::GetScale(new_scale);
      quantizer.Configure(quant_scale, 0xffff);
      scale_size = quant_scale.num_notes;  // Track this scale size for octaves and display
    }

/*
    // Determine approximately where a pitch index for the current scale should be mapped
    // onto a semitone scale keyboard (just for display purposes, mostly so western note subset scales look right!)
    int map_cv_to_piano_pitch(playing_scale_note_index)
    {
      // Get details from current scale
      const braids::Scale & quant_scale = OC::Scales::GetScale(scale);
      int num_notes = quant_scale.num_notes;


      // This won't work: negative offsets go backwards in scale
      //eplaying_scale_note_index %= num_notes;  // Trim down to root index

      //int playing_cv = 

      const braids::Scale & semitone_scale = OC::Scales::GetScale(OC::Scales::SCALE_SEMI);
      
      
      
    }
  */
  
    void DrawGraphics()
    {
      // Wiggle the icon when the sequence regenerates
      int heart_y = 15;
      int die_y = 15;
      if(rand_apply_anim > 0)
      {
        --rand_apply_anim;
        // First the heart jumps, then the die if not locked
        if(rand_apply_anim > 20)
        {
          heart_y = 13;
        }
        else
        {
          die_y = 13;
        }
      }

      // Heart represents the seed/favorite
      //gfxBitmap(1, heart_y, 8, FAVORITE_ICON);
      gfxBitmap(4, heart_y, 8, FAVORITE_ICON);
  
      // Indicate if seed is randomized on reset pulse, or if it's locked for user editing
      // (If unlocked, this also wiggles on regenerate because the seed has been randomized)
      //gfxBitmap(13, (lock_seed ? 15 : die_y), 8, (lock_seed ? LOCK_ICON : RANDOM_ICON));
      gfxBitmap(15, (lock_seed ? 15 : die_y), 8, (lock_seed ? LOCK_ICON : RANDOM_ICON));
  
      // Show the 16-bit seed as 4 hex digits
      int disp_seed = seed;   //0xABCD // test display accuracy
      char sz[2]; sz[1] = 0;  // Null terminated string for easy print
      //gfxPos(24, 15);
      gfxPos(25, 15);
      for(int i=3; i>=0; --i)
      {
        // Grab each nibble in turn, starting with most significant
        int nib = (disp_seed >> (i*4))& 0xF;
        if(nib<=9)
        {
          gfxPrint(nib);
        }
        else
        {
          sz[0] = 'a' + nib - 10;
          gfxPrint(static_cast<const char*>(sz));
        }
      }
  
  

      // Density 
      int gate_dens = get_on_off_density();
      int pitch_dens = get_pitch_change_density();

      //gfxLine(9,36, 29, 36, true); // dotted line
      int xd = 5 + 7-gate_dens;
      int yd = (64*pitch_dens)/256;  // Multiply for better fidelity
      gfxBitmap(12-xd, 27+yd, 8, NOTE4_ICON);
      gfxBitmap(12, 27-yd, 8, NOTE4_ICON);
      gfxBitmap(12+xd, 27, 8, NOTE4_ICON);
      
      if(density < 7)
      {
        //gfxPrint(33, 25, "-");  // Print minus sign this way to right-align the number
        gfxPrint(8, 37, "-");  // Print minus sign this way to right-align the number
      }
      //gfxPrint(40, 25, gate_dens);
      gfxPrint(14, 37, gate_dens);
      
      // Indicate if cv is controlling the density (and locking out manual settings)
      if(density_cv_lock)
      {
        //gfxBitmap(49, 25+2, 8, CV_ICON);
        gfxBitmap(22, 37, 8, CV_ICON);
      }
      

      // Scale and root note select
      //gfxPrint(12, 35, OC::scale_names_short[scale]);
      gfxPrint(39, 27, OC::scale_names_short[scale]);
      
      
      gfxPrint(45, 36, OC::Strings::note_names_unpadded[root]);
 
      //gfxPrint(" (");gfxPrint(density);gfxPrint(")");  // Debug print of actual density value
  
      // Current / total steps
      int display_step = step+1;  // Protocol droids know that humans count from 1
      //gfxPrint(1 + pad(100,display_step), 45, display_step); gfxPrint("/");gfxPrint(num_steps);  // Pad x enough to hold width steady
      gfxPrint(1+pad(10,display_step), 47, display_step); gfxPrint("/");gfxPrint(num_steps);  // Pad x enough to hold width steady
  
      // Show note index (TODO: Tidy)
      //gfxPrint(41, 55, notes[step]);
      int note = notes[step];
      if(note < 0)
      {
        //note += scale_size;  // Convert to lower octave
        gfxBitmap(41, 54, 8, DOWN_BTN_ICON);
      }
      else if(note >= scale_size)
      {
        //note += scale_size;
        gfxBitmap(41, 54, 8, UP_BTN_ICON);
      }

      // Constrain to the scale
//      note = note % scale_size;
//      gfxPrint(49, 55, note);
        gfxPrint(49, 55, curr_step_semitone);
      
      //gfxBitmap(1, 55, 8, CV_ICON); gfxPos(12, 55); gfxPrintVoltage(pitches[step]);
      

      // Figure out what available semitone piano pitch is closest to the current step's issued pitch
      // To cram it onto a piano keyboard visually

// TODO
      
      //int iPlayingIndex = (root + notes[step]) % 12;  // TODO: use current scale modulo?
      // Draw notes
  
      // Draw a TB-303 style octave of a piano keyboard, indicating the playing pitch % by octaves
      
      // TODO: This is not accurate! Indices won't work for e.g. pentatonic scale since they should skip notes here
      // Try a cv lookup of the index, followed by quantization to oct/12 instead?


      int keyboard_pitch = curr_step_semitone -4;  // Translate from 0v
      
      int x = 1; //4
      int y = 61;
      int keyPatt = 0x054A; // keys encoded as 0=white 1=black, starting at c, backwards:  b  0 0101 0100 1010
      for(int i=0; i<12; ++i)
      {
        // Black key?
        y = ( keyPatt & 0x1 ) ? 56 : 61;
        keyPatt >>= 1;
        
        // Two white keys in a row E and F
        if( i == 5 ) x+=3;
  
        //if(iPlayingIndex == i && step_is_gated(step))  // Only render a pitch if gated
        if(curr_step_semitone == i && step_is_gated(step))  // Only render a pitch if gated
        {
          //gfxFrame(x-1, y-1, 5, 4);  // Larger outline frame
          gfxRect(x-1, y-1, 5, 4);  // Larger box
          
        }
        else
        {
          gfxRect(x, y, 3, 2);  // Small filled box
        }
        x += 3;
      }

      // Indicate if the current step has an accent
      if(step_is_accent(step))
      {
        gfxPrint(37, 46, "!");
      }

      // Indicate if the current step has a slide
      if(step_is_slid(step))
      {
          gfxBitmap(42, 46, 8, BEND_ICON);
      }
  
      // Show that the "slide circuit" is actively
      // sliding the pitch (one step after the slid step)
      if(curr_pitch_cv != slide_end_cv)
      {
        gfxBitmap(52, 46, 8, WAVEFORM_ICON);
      }
     
          
      // Draw edit cursor
      if (cursor == 0)
      {
        // Set length to indicate edit
        gfxCursor(14, 23, lock_seed ? 12 : 35); // Seed = auto-randomize / locked-manual
      }
      else if (cursor <= 4) // seed, 4 positions (1-4)
      {
        gfxCursor(24 + 6*(cursor-1), 23, 8);
      }
      else if(cursor == 5)
      {
        //gfxCursor(32, 33, 18);  // density
        gfxCursor(9, 34, 14);  // density
      }
      else if(cursor == 6)
      {
        //gfxCursor(12, 43, 26);  // scale
        gfxCursor(38, 34, 26);  // scale
      }
      else if(cursor == 7)
      {
       //gfxCursor(49, 43, 12);  // root note
       gfxCursor(44, 43, 14);  // root note
      }
      else if(cursor == 8)
      {
        gfxCursor(20, 54, 12);  // step
      }
    }

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
