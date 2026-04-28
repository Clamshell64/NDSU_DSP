/**************************************************************************/
//Name:  main.c	
//Purpose:  Main program for IIR filter HW6
//Author: Dylan Malsed																									
//Target:  Freescale K22f						
/**************************************************************************/

#include "MK22F51212.h"                 				//Device header
#include "MCG.h"																//Clock header
#include "TimerInt.h"														//Timer Interrupt Header
#include "ADC.h"																//ADC Header
#include "DAC.h"
#include "coef.h"													//DAC Header

#define SW2_PIN		1u	// PORTB Pin 1
#define SW3_PIN		17u	// PORTC Pin 17
#define DAC_MID     2048 // mid scale for 12 bit DAC

uint8_t filter_mode = 0; // 0 = digital wire || 1 = f1: 250Hz || 2 = f1: 500Hz || 3 = f1: 1000Hz || 4 = f1: 2000Hz

uint8_t sw2_pressed = 0;
uint8_t sw3_pressed = 0;

uint16_t adc_measurement;

uint16_t buffer_idx = 0; // index for circular buffer of coefficients
uint16_t buffer[L]; // circular buffer to hold past L samples for FIR filter (1 for each order); stored in Q15 format


/* small software delay utility function */
static void delay(volatile uint32_t d){ while(d--) __NOP(); }


int16_t convert_to_q15(uint16_t input){
	int16_t out = (int16_t)input - DAC_MID;
	out = out << 4; // shift left 4 to get to q15
	// clamp
	if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;
	return out;
}


uint16_t process_fixed_sample(uint16_t input){
    int64_t acc = 0; // use a 64-bit value to account for overflow so we can have more accuracy during intermediate steps

    int16_t x = convert_to_q15(input);

    buffer[buffer_idx] = x;

    // digital wire (passthrough option)
    if (filter_mode == 0){
        buffer_idx = (buffer_idx + 1) % L;
        return input;
    }

    // FIR
    for (int i = 0; i < L; i++){
        int idx = (buffer_idx - i + L) % L;
        acc += buffer[idx] * coef[i]; // q15*q15 = q30
    }

    buffer_idx = (buffer_idx + 1) % L;

	acc = acc >> 15;
    // clamp output
    if (acc > 32767) acc = 32767;
    if (acc < -32768) acc = -32768;
    // back to DAC range
    return (uint16_t)((acc>>4) + DAC_MID);
}


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


void PIT0_IRQHandler(void){	// 10kS interrupt
	NVIC_ClearPendingIRQ(PIT0_IRQn);							//Clears interrupt flag in NVIC Register
	PIT->CHANNEL[0].TFLG	= PIT_TFLG_TIF_MASK;		//Clears interrupt flag in PIT Register		

	PTA->PSOR = (1u<<1); // Red LED off; stay off for duration of processing during interrupt
	/* ----------  ADC READ ---------- */
	adc_measurement = ADC0->R[0]; // read conversion result
	ADC0->SC1[0] = ADC_SC1_ADCH(0); // set flag to start ADC conversion	
	/* ----------- DAC WRITE --------- */
	/* output to dac */ 
	// apply DC offset to ensure filter method works (center the sample around 0 then process filter)
	uint16_t conditioned_sample = process_fixed_sample(adc_measurement);

	DAC_SetRaw(conditioned_sample);
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
	
	while(1){
		// button test
		
		if (SW2_Pressed() && !sw2_pressed){
			// call this code once when switch is pressed
			sw2_pressed = 1;
			filter_mode = (filter_mode + 1) % 2;
		}else if (sw2_pressed){
			delay(10000); // simple & quick debounce method
			if (!SW2_Pressed()){
				sw2_pressed = 0;
			}
		}

		if (SW3_Pressed() && !sw3_pressed){
			// call this code once when switch is pressed
			sw3_pressed = 1;
			filter_mode = (filter_mode - 1 + 2) % 2; // wrap backwards
		}else if (sw3_pressed){
			delay(10000); // simple & quick debounce method
			if (!SW3_Pressed()){
				sw3_pressed = 0;
			}
		}
	}
}


