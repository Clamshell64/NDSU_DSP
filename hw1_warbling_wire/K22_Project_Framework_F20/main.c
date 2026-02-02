/**************************************************************************/
//Name:  main.c																														//
//Purpose:  Skeleton project with configuration for ADC, DAC, MCG and PIT	//
//Author:  Ethan Hettwer																									//
//Revision:  1.0 15Sept2014 EH Initial Revision														//
//Target:  Freescale K22f																									//
/**************************************************************************/

#include "MK22F51212.h"                 				//Device header
#include "MCG.h"																//Clock header
#include "TimerInt.h"														//Timer Interrupt Header
#include "ADC.h"																//ADC Header
#include "DAC.h"																//DAC Header

uint16_t adc_measurement;
uint16_t pit_counter = 0;
const uint16_t pit_counter_max = 20000; // 2 seconds worth of interrupts @ 10kHz
float warble_factor = 0.1f; // scale amplitude of signal; 10% to 100%. Start at 10%

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


void PIT0_IRQHandler(void){	//This function is called when the timer interrupt expires
	//Place Interrupt Service Routine Here
	NVIC_ClearPendingIRQ(PIT0_IRQn);							//Clears interrupt flag in NVIC Register
	PIT->CHANNEL[0].TFLG	= PIT_TFLG_TIF_MASK;		//Clears interrupt flag in PIT Register		
	
	/* ----------  ADC READ ---------- */
	ADC0->SC1[0] = ADC_SC1_ADCH(0); // set flag to start ADC conversion
	
	while(!(ADC0->SC1[0] & ADC_SC1_COCO_MASK)); // wait for conversion complete
	adc_measurement = ADC0->R[0]; // read conversion result
	/* ----------- DAC WRITE --------- */

	// ---------- calculate warble based on pit_counter ----------
	if (pit_counter < (pit_counter_max / 2)){
		//going up
		warble_factor = 0.1f + (0.9f * (float)pit_counter/((float)pit_counter_max/2));
		if (warble_factor > 1.0f) warble_factor = 1.0f; // cap at 100%
	} else {
		//going down
		warble_factor = 1.0f - (0.9f * (float)(pit_counter - pit_counter_max/2)/((float)pit_counter_max/2));
		if (warble_factor < 0.1f) warble_factor = 0.1f; // cap at 10%
	}
	// --------------------------------------------------

	// output dac value with warble applied
	DAC_SetRaw((uint16_t)((float)adc_measurement * warble_factor)); // write ADC reading to DAC
	/* ------------------------------- */
	
	// loop pit counter
	pit_counter++;
	if(pit_counter >= pit_counter_max){
		pit_counter = 0;
	}
}


int main(void){
	MCG_Clock120_Init();
	ADC_Init();
	ADC_Calibrate();
	DAC_Init();
	TimerInt_Init();
	LED_Init();
	
	while(1){
		// LED_cycle();
		// for (uint16_t v = 0; v <= 4095u; ++v){
		// 	DAC_SetRaw(v);
		// 	delay(10000u); // ~pause so you can observe each voltage
		// }
	}
}
