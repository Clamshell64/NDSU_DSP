/**************************************************************************/
//Name:  main.c	
//Purpose:  Main Program for Guitar Harmonizer Pedal DSP final Project (ECE 444/644)
//Author: Dylan Malsed																									
//Target:  Freescale K22f						
/**************************************************************************/

#include "MK22F51212.h"   //Device header
#include "MCG.h"	      //Clock header
#include "TimerInt.h"	  //Timer Interrupt Header
#include "ADC.h"		  //ADC Header
#include "DAC.h"
#include "math.h"

#define PI 3.14159265358979323846f
#define SW2_PIN		1u	// PORTB Pin 1
#define SW3_PIN		17u	// PORTC Pin 17
#define DAC_MID     2048 // mid scale for 12 bit DAC
#define FS ((int)(60000000/INTERRUPT_CLOCKS))
#define MAX_DELAY_MS 40 // how many ms back can we send the read heads
#define MAX_DELAY_SAMPLES ((int)(FS * (MAX_DELAY_MS / 1000.0f)))
#define BUFFER_LENGTH (MAX_DELAY_SAMPLES + 4)
#define CROSSFADE_LEN ((int)(0.02 * FS)) // length of the crossfade window: 20ms * sample rate

// switch buttons
uint8_t sw2_pressed = 0;
uint8_t sw3_pressed = 0; // allows us to choose pitch shift

float pitch_factor = 1.015f;
float wet_mix = 0.5;
int delay_buffer[BUFFER_LENGTH];

// crossfade hann window
// fade in: idx 0:(CROSSFADE_LEN-1)
// fade out: idx CROSSFADE_LEN:(CROSSFADE_LEN*2 - 1)
float crossfade_win[(int)CROSSFADE_LEN * 2];

int write_idx = 0;
// read indices for crossfade heads; stored as floats in order to utilize sample interpolation 
float rp1 = 0;
float rp2 = 0;
float delay1 = (float)MAX_DELAY_SAMPLES;
float delay2 = (float)MAX_DELAY_SAMPLES/2;
// count crossfade samples if we are crossfading between two read heads
// if 0, we just use crossfade_state to get 100% of the output audio for pitch shifting
int crossfade_ctr = 0; 

 //which read head are we getting 100% of our audio from?
 //*IF* we are not crossfading between two read heads, this will tell which one it is.
	0 = read head 1; 1 = read head 2 */
int crossfade_state = 0; 


// small software delay utility function (used for onboard switch debounce)
static void delay(volatile uint32_t d){ while(d--) __NOP(); }


// helper method to wrap an int around at a certain fixed-point value(used for circular buffer indexing)
int buffer_wrap_int(int n, int wrap_val){
	int wrapped = n;
	if (wrapped >= wrap_val){
    	wrapped -= wrap_val;
	}else if (wrapped < 0){
    	wrapped += wrap_val;
	}
	return wrapped;
}

// helper method to wrap an float around at a certain fixed-point value(used for circular buffer indexing)
float buffer_wrap_float(float n, float wrap_val){
	float wrapped = n;
	if (wrapped >= wrap_val){
    	wrapped -= wrap_val;
	}else if (wrapped < 0){
    	wrapped += wrap_val;
	}
	return wrapped;
}

// fill an array with a hann window
void init_hann_window(float *window, int N){
	for (int n = 0; n<N; n++){
		window[n] = 0.5f * (1.0f - cosf((2.0f * PI * n) / (N - 1)));
	}
}

// initialize RGB LED pins
void LED_Init(void){ 
	SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTD_MASK;

	/* set LED pins to GPIO */
	PORTA->PCR[1] = PORT_PCR_MUX(1);  /* PTA1 - Red */
	PORTA->PCR[2] = PORT_PCR_MUX(1);  /* PTA2 - Green*/
	PORTD->PCR[5] = PORT_PCR_MUX(1);  /* PTD5 - Blue*/

	/* configure as outputs  (PDDR - pin direction: 1 = output)*/
	PTA->PDDR |= (1u<<1) | (1u<<2);
	PTD->PDDR |= (1u<<5);

	/* default to OFF ---- PSOR = set (off), PCOR = clear (on) ---- onboard RGB LED is ACTIVE LOW */
	PTA->PSOR = (1u<<1) | (1u<<2);
	PTD->PSOR = (1u<<5);
}

// initialize onboard pushbutton switches (switches active low)
void onboard_Pushbutton_Init(void){ 
	SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK | SIM_SCGC5_PORTC_MASK; // SW2 on PORTB, SW3 on PORTC
	// SW2 is the button towards the center of the board
	PORTC->PCR[1] = PORT_PCR_MUX(1); 
	GPIOC->PDDR &= ~(1 << SW2_PIN);
	// SW3 is the button towards the edge of the board
	PORTB->PCR[17] = PORT_PCR_MUX(1); 
	GPIOB->PDDR &= ~(1 << SW3_PIN);
}

// pushbutton helpers
int SW2_Pressed(void){
	return !(PTC->PDIR & (1u<<SW2_PIN)); // return true if SW2 is pressed (active low)
}
int SW3_Pressed(void){
	return !(PTB->PDIR & (1u<<SW3_PIN)); // return true if SW3 is pressed (active low)
}

void PIT0_IRQHandler(void){	
	NVIC_ClearPendingIRQ(PIT0_IRQn);							//Clears interrupt flag in NVIC Register
	PIT->CHANNEL[0].TFLG	= PIT_TFLG_TIF_MASK;		//Clears interrupt flag in PIT Register		

	PTA->PSOR = (1u<<1); // Red LED off; stay off for duration of processing during interrupt
	/* ----------  ADC READ ---------- */
	uint16_t adc_in = ADC0->R[0]; // read conversion result
	ADC0->SC1[0] = ADC_SC1_ADCH(0); // set flag to start ADC conversion	
	/* ----------- DELAY LOGIC & DAC WRITE --------- */
	uint16_t output = 0;

	// put the latest adc reading into the circular buffer
	delay_buffer[write_idx] = adc_in - DAC_MID;
	
	// read from two different points behind (time-wise) the write pointer 
	// (behind as in time; they could be in front of write_idx in terms of the circular buffer)
	rp1 = (float)write_idx - delay1;
	rp2 = (float)write_idx - delay2;
	// wrap the indices as needed, it's possible that the delay pushed them negative
	rp1 = buffer_wrap_float(rp1, BUFFER_LENGTH);
	rp2 = buffer_wrap_float(rp2, BUFFER_LENGTH);

	// interpolate between floor and ceil versions of read indices based on the read index floating-point part.
	// calculate the fractional portion of read indices
	uint16_t rp1_a = floor(rp1); // will never need buffer wrap
	uint16_t rp1_b = buffer_wrap_int(rp1_a + 1, BUFFER_LENGTH); // could need buffer wrap if it gets pushed over buffer length
	float rp1_frac = rp1 - rp1_a; // how far along to rp1_b is rp1?

	// same thing for 2nd read head
	uint16_t rp2_a = floor(rp2);
	uint16_t rp2_b = buffer_wrap_int(rp2_a + 1, BUFFER_LENGTH);
	float rp2_frac = rp2 - rp2_a;

	// use linear interpolation
	int rp1_sample = ((1 - rp1_frac) * delay_buffer[rp1_a]) + (rp1_frac * delay_buffer[rp1_b]);
	int rp2_sample = ((1 - rp2_frac) * delay_buffer[rp2_a]) + (rp2_frac * delay_buffer[rp2_b]);

	// if we are in a crossfade range, apply different weights to rp1,2_sample to get output
	// if we are NOT in a crossfade range, choose sample based on crossfade state
	if (crossfade_ctr > 0){
		if (crossfade_state == 0){
			//                     fade in                                             fade out
 			output = (crossfade_win[crossfade_ctr] * rp2_sample) + (crossfade_win[crossfade_ctr+CROSSFADE_LEN] * rp1_sample);
		}else if (crossfade_state == 1){
			//                     fade in                                             fade out
			output = (crossfade_win[crossfade_ctr] * rp1_sample) + (crossfade_win[crossfade_ctr+CROSSFADE_LEN] * rp2_sample);
		}
		crossfade_ctr++;
		crossfade_ctr = buffer_wrap_int(crossfade_ctr, CROSSFADE_LEN);
		// just wrapped back around to 0; crossfade is now finished. Switch crossfade state to change which read head is now fully active
		if (crossfade_ctr == 0){
			crossfade_state = !crossfade_state;
		}
	}else{
		if (crossfade_state == 0){
			output = rp1_sample;
		}else if (crossfade_state == 1){
			output = rp2_sample;
		}
	}

	// update delay values that will be applied to read head indices
	delay1 += (1.0f-pitch_factor);
	delay2 += (1.0f-pitch_factor);

	// check boundaries to see if it's time to crossfade

	if (pitch_factor > 1){
		// check when to start a crossfade
		if (delay1 <= 0 && (crossfade_state==1) && (crossfade_ctr==0)){
			crossfade_ctr = 1;
			delay1 = 0;
		}else if (delay2 <= 0 && (crossfade_state==0) && (crossfade_ctr==0)){
			crossfade_ctr = 1;
			delay2 = 0;
		}
		// if delay too small, reset it to be max delay samples (the start of the crossfade)
		if (delay1 <= -0.000001){
			delay1 = delay1 + MAX_DELAY_SAMPLES;
		}
		if (delay2 <= -0.000001){
			delay2 = delay2 + MAX_DELAY_SAMPLES;
		}
	}

	// mix wet and dry signals
	output = (wet_mix * output) + ((1-wet_mix)* adc_in);

	output += DAC_MID; // apply dac midpoint again


	if (effect_mode == 6){
		DAC_SetRaw(adc_in); // bypass mode
	}else {
		DAC_SetRaw(output);
	}
	// increment the circular buffer write index
	write_idx++;
	write_idx = buffer_wrap_int(write_idx, BUFFER_LENGTH);

	/* ------------------------------- */
	PTA->PCOR = (1u<<1); // Red LED on; indicate interrupt free time. The brighter the LED, the more processing time is left
}

int main(void){
	MCG_Clock120_Init();
	ADC_Init();
	ADC_Calibrate();
	DAC_Init();
	TimerInt_Init();
	onboard_Pushbutton_Init();
	LED_Init();
	init_hann_window(crossfade_win, CROSSFADE_LEN*2);
	while(1){
		
		switch (effect_mode) {
			case 0:
				pitch_factor = 1.015; // chorus effect
				break;
			case 1:
				pitch_factor = 1.189; // +3 semitones (minor third)
				break;
			case 2:
				pitch_factor = 1.2599; // +4 semitones (major third)
				break;
			case 3:
				pitch_factor = 1.3348; // +5 semitones (perfect fourth)
				break;
			case 4:
				pitch_factor = 1.4983; // +7 semitones (perfect fifth)
				break;
			case 5:
				pitch_factor = 2.0; // + 12 semitones (one octave)
			default:
				// code to execute if expression doesn't match any case
		}

		if (SW2_Pressed() && !sw2_pressed){
			// call this code once when switch is pressed
			sw2_pressed = 1;
			effect_mode = (effect_mode + 1) % 7;
		}else if (sw2_pressed){
			delay(10000); // simple & quick debounce method
			if (!SW2_Pressed()){
				sw2_pressed = 0;
			}
		}

		if (SW3_Pressed() && !sw3_pressed){
			// call this code once when switch is pressed
			effect_mode = (effect_mode - 1 + 2) % 6; // wrap backwards
		}else if (sw3_pressed){
			delay(10000); // simple & quick debounce method
			if (!SW3_Pressed()){
				sw3_pressed = 0;
			}
		}
	}
}