/*
* ornament + crime // 4xCV DAC8565  // "ASR" 
*
* --------------------------------
* TR 1 = clock
* TR 2 = hold
* TR 3 = oct +
* TR 4 = oct -
*
* CV 1 = sample in
* CV 2 = index CV
* CV 3 = # notes (constrain)
* CV 4 = octaves/offset
*
* left  encoder = scale select
* right encoder = param. select
* 
* button 1 (top) =  oct +
* button 2       =  oct -
* --------------------------------
*
*/

#include <spi4teensy3.h>
#include <u8g_teensy.h>
#include <rotaryplus.h>
#include <EEPROM.h>

#define CS 10  // DAC CS 
#define RST 9  // DAC RST

#define CV1 19
#define CV2 18
#define CV3 20
#define CV4 17

#define TR1 0
#define TR2 1
#define TR3 2
#define TR4 3

#define encR1 15
#define encR2 16
#define butR  14

#define encL1 22
#define encL2 21
#define butL  23

#define but_top 5
#define but_bot 4

U8GLIB u8g(&u8g_dev_sh1106_128x64_2x_hw_spi, u8g_com_hw_spi_fn);

Rotary encoder[2] =
{
  {encL1, encL2}, 
  {encR1, encR2}
}; 

//  UI mode select
extern uint8_t UI_MODE;

/*  ------------------------ ASR ------------------------------------  */

#define MAX_VALUE 65535 // DAC fullscale 
#define MAX_ITEMS 256   // ASR ring buffer size
#define OCTAVES 10      // # octaves
uint16_t octaves[OCTAVES+1] = {0, 6553, 13107, 19661, 26214, 32768, 39321, 45875, 52428, 58981, 65535}; // in practice  
const uint16_t THEORY[OCTAVES+1] = {0, 6553, 13107, 19661, 26214, 32768, 39321, 45875, 52428, 58981, 65535}; // in theory  
extern const uint16_t _ZERO;

typedef struct ASRbuf
{
    uint8_t     first;
    uint8_t     last;
    uint8_t     items;
    uint16_t data[MAX_ITEMS];

} ASRbuf;

ASRbuf *ASR;

/*  ---------------------  CV   stuff  --------------------------------- */

#define _ADC_RATE 1000
#define _ADC_RES  12
#define numADC 4
int16_t cvval[numADC];                        // store cv values
uint8_t cvmap[numADC] = {CV1, CV1, CV3, CV4}; // map ADC pins

// PIT timer : 
IntervalTimer ADC_timer;
volatile uint16_t _ADC = false;

void ADC_callback()
{ 
  _ADC = true; 
}

/*  --------------------- clk / buttons / ISR -------------------------   */

uint32_t _CLK_TIMESTAMP = 0;
uint32_t _BUTTONS_TIMESTAMP = 0;
const uint16_t TRIG_LENGTH = 150;
const uint16_t DEBOUNCE = 250;

volatile uint16_t CLK_STATE1;

void FASTRUN clk_ISR()
{  
    CLK_STATE1 = true; 
}  // main clock

enum the_buttons 
{  
  BUTTON_TOP,
  BUTTON_BOTTOM,
  BUTTON_LEFT,
  BUTTON_RIGHT
};  

volatile boolean _ENC = false;
const uint16_t _ENC_RATE = 15000;

IntervalTimer ENC_timer;
void ENC_callback() 
{ 
  _ENC = true; 
} // encoder update 


/*       ---------------------------------------------------------         */

void setup(){
  
  NVIC_SET_PRIORITY(IRQ_PORTB, 0); // TR1 = 0 = PTB16
  analogReadResolution(_ADC_RES);
  analogReadAveraging(4);
  spi4teensy3::init();
  
  // pins 
  pinMode(butL, INPUT);
  pinMode(butR, INPUT);
  pinMode(but_top, INPUT);
  pinMode(but_bot, INPUT);
 
  pinMode(TR1, INPUT); // INPUT_PULLUP);
  pinMode(TR2, INPUT);
  pinMode(TR3, INPUT);
  pinMode(TR4, INPUT);
  
  // clock ISR 
  attachInterrupt(TR1, clk_ISR, FALLING);
  // encoder ISR 
  attachInterrupt(encL1, left_encoder_ISR, CHANGE);
  attachInterrupt(encL2, left_encoder_ISR, CHANGE);
  attachInterrupt(encR1, right_encoder_ISR, CHANGE);
  attachInterrupt(encR2, right_encoder_ISR, CHANGE);
  // ADC timer
  ADC_timer.begin(ADC_callback, _ADC_RATE);
  ENC_timer.begin(ENC_callback, _ENC_RATE);
  // set up DAC pins 
  pinMode(CS, OUTPUT);
  pinMode(RST,OUTPUT);
  // pull RST high 
  digitalWrite(RST, HIGH); 
  // set all outputs to zero 
  delay(10);
  set8565_CHA(_ZERO);
  set8565_CHB(_ZERO);
  set8565_CHC(_ZERO);
  set8565_CHD(_ZERO);
  // splash screen, sort of ... 
  hello(); 
  // calibrate? else use EEPROM; else use things in theory :
  if (!digitalRead(butL))  calibrate_main();
  else if (EEPROM.read(0x2) > 0) read_settings(); 
  else in_theory(); // uncalibrated DAC code 
  delay(1250);   
  // initialize ASR 
  init_DACtable();
  ASR = (ASRbuf*)malloc(sizeof(ASRbuf));
  init_ASR(ASR);  
}


/*  ---------    main loop  --------  */

//uint32_t testclock;

void loop(){
 
   while(1) 
   {
     _loop();
   }
}



