# Changes to the stock firmware:

## Removed Applications:
- Captain MIDI is disabled to make room for new applets.

## Changes to existing Hemisphere Applets:
Logarhythm Branch applies several changes to some of the stock Hemisphere Applets, adding a few new features and some subjective tweaks for preference.

### Shuffle
CV output B (formerly unused) now outputs Triplets. It detects the clock tempo and emits 3 triplet pulses evenly distributed across 4 clocks, starting at the first input clock. The reset input resets this count to 0 along with the shuffle output's odd/even beat. No input parameters affect the triplets-- it's just a bonus output that can be fiddly to patch normally and pairs well with swing.

### ShiftRegister (Turing Machine)
An output range parameter has been added, allowing you to constrain the pitch output range from 1-32 scale notes instead of always having a range of 32. This parameter is not saved due to space restrictions.

### Step5
Holding reset input high will suppress clock triggers, similar to an analogue sequencer.