/**************************************************************************/
//Name:  main.c	
//Purpose:  Main Program for Delay Pedal DSP final Project
//Author: Dylan Malsed																									
//Target:  Freescale K22f						
/**************************************************************************/

#include "MK22F51212.h"                 				//Device header
#include "MCG.h"																//Clock header
#include "TimerInt.h"														//Timer Interrupt Header
#include "ADC.h"																//ADC Header
#include "DAC.h"
#include "math.h"
//#include "coef.h"													//DAC Header

#define SW2_PIN		1u	// PORTB Pin 1
#define SW3_PIN		17u	// PORTC Pin 17
#define DAC_MID     2048 // mid scale for 12 bit DAC

#define PI 3.14159265359f
#define BUFFER_SIZE 22000 // ~0.5 sec @ 44.1kHz
#define WIN_SIZE 1000 // 30 ms window size in samples
#define SAFETY_MARGIN 20 // extra samples to ensure we don't read uninitialized data due to phase wrap

#define pitch_ratio 3.9f
float phase_inc = 1.0f/WIN_SIZE; // phase increments over the window on a 0-1 scale. This is so we don't have to do indexing math in the interrupt.
float delay_offset = (WIN_SIZE * pitch_ratio) + SAFETY_MARGIN; // how far behind to set the read index for the delay effect (in samples)
float hann_table[WIN_SIZE];

float phase1 = 0.0f;
float read_idx1 = 0.0f; // use a float for the read index so we can do fractional increments with pitch_ratio. We will use interpolation to get the sample value.

float phase2 = 0.5f; // 180 degree phase shift for second read index
float read_idx2 = 0.0f;

uint32_t write_idx = 0; // index for writing new samples into the buffer
int16_t buffer[BUFFER_SIZE]; // circular buffer to hold audio samples

uint8_t sw2_pressed = 0;
uint8_t sw3_pressed = 0;

uint16_t adc_measurement;
uint8_t effect_mode = 0; // 0=nothing, 1=delay

uint16_t output;
float weight1;
float weight2;

float lp_out = 0.0f;
float alpha = 0.2f;  // lower = more smoothing


/* small software delay utility function */
static void delay(volatile uint32_t d){ while(d--) __NOP(); }


void LED_Init(void){ // initialize RGB LED pins
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


void LED_cycle(void){
	/* PTA1 */
	PTA->PCOR = (1u<<1); delay(4000000); PTA->PSOR = (1u<<1); delay(2000000);
	/* PTA2 */
	PTA->PCOR = (1u<<2); delay(4000000); PTA->PSOR = (1u<<2); delay(2000000);
	/* PTD5 */
	PTD->PCOR = (1u<<5); delay(4000000); PTD->PSOR = (1u<<5); delay(2000000);
	delay(100000);
}


void onboard_Pushbutton_Init(void){ // initialize onboard pushbutton switches (switches active low)
	SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK | SIM_SCGC5_PORTC_MASK; // SW2 on PORTB, SW3 on PORTC
	// SW2 is the button towards the center of the board
	PORTC->PCR[1] = PORT_PCR_MUX(1); 
	GPIOC->PDDR &= ~(1 << SW2_PIN);
	// SW3 is the button towards the edge of the board
	PORTB->PCR[17] = PORT_PCR_MUX(1); 
	GPIOB->PDDR &= ~(1 << SW3_PIN);
}


int SW2_Pressed(void){
	return !(PTC->PDIR & (1u<<SW2_PIN)); // return true if SW2 is pressed (active low)
}


int SW3_Pressed(void){
	return !(PTB->PDIR & (1u<<SW3_PIN)); // return true if SW3 is pressed (active low)
}


void init_hann() {
    for (int i = 0; i < WIN_SIZE; i++) {
        float phase = (float)i / WIN_SIZE;
        hann_table[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159f * phase));
    }
}


float get_hann(float phase) {
    if (phase < 0.0f) phase = 0.0f;
    if (phase >= 1.0f) phase = 0.9999f;
    int idx = (int)(phase * WIN_SIZE);
    return hann_table[idx];
}


float buffer_wrap_add(float idx, float offset){
	float result = idx + offset;
	if (result >= BUFFER_SIZE){
		result -= BUFFER_SIZE;
	} else if (result < 0){
		result += BUFFER_SIZE;
	}
	return result;
}


int16_t sample_frac_interp(float idx){
	// get the integer and fractional parts of the read index
	uint32_t idx_floor = (uint32_t)idx;
	uint32_t idx_ceil  = (idx_floor + 1) % BUFFER_SIZE;  // wrap if at end of buffer
	float frac = idx - idx_floor;

	int16_t sample = (int16_t)(buffer[idx_floor] * (1.0f - frac) + buffer[idx_ceil] * frac);
	return sample;
}


void PIT0_IRQHandler(void){	// 10kS interrupt
	NVIC_ClearPendingIRQ(PIT0_IRQn);							//Clears interrupt flag in NVIC Register
	PIT->CHANNEL[0].TFLG	= PIT_TFLG_TIF_MASK;		//Clears interrupt flag in PIT Register		

	PTA->PSOR = (1u<<1); // Red LED off; stay off for duration of processing during interrupt
	/* ----------  ADC READ ---------- */
	uint16_t adc_measurement = ADC0->R[0]; // read conversion result
	ADC0->SC1[0] = ADC_SC1_ADCH(0); // set flag to start ADC conversion	
	/* ----------- DELAY LOGIC & DAC WRITE --------- */
	
	/*
	keep a circular buffer of the last 20,000 samples (1 sec @ 20kS/s).
	output the current sample to DAC just like usual, but to add delay, add the sample from 20,000 samples ago to the output
	*/

	buffer[write_idx] = adc_measurement - DAC_MID;

	// -------- grab the samples we want from the buffer using the read indices, and apply the Hann window to them --------

	int16_t sample_1 = sample_frac_interp(read_idx1); //buffer[(uint32_t)read_idx1]; // replace this with interpolation for fractional read index later
	int16_t sample_2 = sample_frac_interp(read_idx2); //buffer[(uint32_t)read_idx2];
	
	weight1 = get_hann(phase1);
	weight2 = get_hann(phase2);

	output = (uint16_t)((weight1 * sample_1) + (weight2 * sample_2) + DAC_MID); // set output to be the weighted sum of the samples (using the Hann window)

	// float dry = (float)(adc_measurement - DAC_MID);
	// float wet = (weight1 * sample_1) + (weight2 * sample_2);

	// float mix = (0.5f * dry) + (0.5f * wet); // 50/50 blend

	// if (mix > 2047.0f)  mix = 2047.0f;
	// if (mix < -2048.0f) mix = -2048.0f;
	// output = (uint16_t)(mix + DAC_MID);

	// -------- increment the read and write indices, wrapping as needed --------
	read_idx1 = buffer_wrap_add(read_idx1, pitch_ratio);
	read_idx2 = buffer_wrap_add(read_idx2, pitch_ratio);
	phase1 += phase_inc;
	phase2 += phase_inc;
	if (phase1 >= 1.0f){
		phase1 -= 1.0f; // wrap phase back to 0 after one full cycle
		read_idx1 = buffer_wrap_add((float)write_idx, -delay_offset); // reset read index to be the correct offset behind the write index
	}
	if (phase2 >= 1.0f){
		phase2 -= 1.0f; // wrap phase back to 0 after one full cycle
		read_idx2 = buffer_wrap_add(read_idx1, -(WIN_SIZE / 2.0f));
	}

	// increment write index with wraparound
	write_idx = (write_idx + 1) % BUFFER_SIZE;

	if (effect_mode == 0){
		DAC_SetRaw(output); // bypass mode
	}else if (effect_mode == 1){
		DAC_SetRaw(adc_measurement); 
	}

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
	init_hann();
	read_idx1 = buffer_wrap_add(write_idx, -delay_offset);
	read_idx2 = buffer_wrap_add(write_idx, -(delay_offset/2)); // second read index is 180 degrees out of phase with the first, so add half the buffer size to the offset
	
	while(1){
		// button test
		
		if (SW2_Pressed() && !sw2_pressed){
			// call this code once when switch is pressed
			sw2_pressed = 1;
			effect_mode = (effect_mode + 1) % 2;
		}else if (sw2_pressed){
			delay(10000); // simple & quick debounce method
			if (!SW2_Pressed()){
				sw2_pressed = 0;
			}
		}

		if (SW3_Pressed() && !sw3_pressed){
			// call this code once when switch is pressed
			effect_mode = (effect_mode - 1 + 2) % 2; // wrap backwards
		}else if (sw3_pressed){
			delay(10000); // simple & quick debounce method
			if (!SW3_Pressed()){
				sw3_pressed = 0;
			}
		}
	}
}


