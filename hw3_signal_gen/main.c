/**************************************************************************/
//Name:  main.c	
//Purpose:  Main program for Spectral Inverter HW2 
//Author: Dylan Malsed																									
//Target:  Freescale K22f						
/**************************************************************************/

#include "MK22F51212.h"                 				//Device header
#include "MCG.h"																//Clock header
#include "TimerInt.h"														//Timer Interrupt Header
#include "ADC.h"																//ADC Header
#include "DAC.h"																//DAC Header
#include <math.h>

#define PI 3.14159265358979323846f

int time = 0; // keep track of period
int K = 10; // order of fourier series approximation
float w0 = 2.0f * PI / 40.0f; 
float c1 = 0.5f;
float c2 = 1.2764f;
float c3 = 5000.0f;

float ck_real[11] = {0.5750, -0.0329, -0.2482, 0.0650, -0.0099, -0.0399, 0.0031, 0.0262, -0.0527, 0.0333, -0.004}; // real coefficients
float ck_imag[11] = {0.0, -0.1354, -0.0874, 0.1322, 0.0853, 0.008, 0.0473, 0.0566, 0.03, 0.0027, -0.0159}; // imaginary coefficients


/* small software delay utility function */
static void delay(volatile uint32_t d){ while(d--) __NOP(); }


void LED_Init(void){ // initialize RGB LED pins
	SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTD_MASK;

	/* set LED pins to GPIO */
	PORTA->PCR[1] = PORT_PCR_MUX(1);  /* PTA1 - Red */
	PORTA->PCR[2] = PORT_PCR_MUX(1);  /* PTA2 - Green*/
	PORTD->PCR[5] = PORT_PCR_MUX(1);  /* PTD5 - Blue*/
	PORTA->PCR[5] = PORT_PCR_MUX(1);  /* PTA5 - general purpose - I'm going to connect an LED to it*/

	/* configure as outputs  (PDDR - pin direction: 1 = output)*/
	PTA->PDDR |= (1u<<1) | (1u<<2) | (1u << 5);;
	PTD->PDDR |= (1u<<5);

	/* default to OFF ---- PSOR = set (off), PCOR = clear (on) ---- onboard RGB LED is ACTIVE LOW */
	PTA->PSOR = (1u<<1) | (1u<<2) | (1u << 5);
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


int yk(int order, int time){
	float y = ck_real[0]; // DC term

	float t = (float)time / 10000.0f; // convert time int counter to a ms scale - 10kHz sample rate

	for (int k=1; k <= order; k++){
		y += 2.0f * ((ck_real[k] * cosf(w0 * k *c3 * t)) - (ck_imag[k] * sinf(w0 * k * c3 * t)));
	}

	y *= c2;
	y += c1;

	// DAC scale - y is in range 0..3, so scale to 0..4095
	int output = (int)((y / 3.0f) * 4095.0f);

	return output;
}



void PIT0_IRQHandler(void){	//This function is called when the timer interrupt expires
	//Place Interrupt Service Routine Here
	NVIC_ClearPendingIRQ(PIT0_IRQn);							//Clears interrupt flag in NVIC Register
	PIT->CHANNEL[0].TFLG	= PIT_TFLG_TIF_MASK;		//Clears interrupt flag in PIT Register		
	
	PTA->PCOR = (1u<<5); // LED on to indicate we're in the ISR

	/* output to dac */ 
	DAC_SetRaw(yk(5, time));
	time = (time + 1) % 80; // period = T/c3 = 40ms, so with 10kHz sample rate, we have 400 samples per period
	//delay(1000);
	PTA->PSOR = (1u<<5); // LED off to indicate we're leaving the ISR
}

// measured ~54 % of the interrupt is taken doing calculations. for k=10
// K = 5 gives takes about 25% of the interrupt time

int main(void){
	MCG_Clock120_Init();
	ADC_Init();
	ADC_Calibrate();
	DAC_Init();
	TimerInt_Init();
	LED_Init();
	
	while(1){
		
		//LED_cycle();
		// for (float v = 0.0f; v <= 2.0f * PI; v += 0.01f){ 
		// 	//4095 max
		// 	float s = sinf(v);
		// 	uint16_t dac = (uint16_t)((s * 0.5f + 0.5f) * 4095.0f); // scale to 0..4095
		// 	DAC_SetRaw(dac);
		// 	delay(10000u); // ~pause so you can observe each voltage
		// }
	}
}
