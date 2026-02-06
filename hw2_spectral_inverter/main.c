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

#define SW2_PIN		1u	// PORTB Pin 1
#define SW3_PIN		17u	// PORTC Pin 17
#define DAC_MID     2048 // mid scale for 12 bit DAC

// flags that set to 1 iff a switch was just pressed
uint8_t sw2_just_pressed = 0;
uint8_t sw3_just_pressed = 0;

uint16_t adc_measurement;
uint16_t dac_bitmask = 0xFFF;//0xFFF; // default to 12-bit resolution (no bits masked)

uint8_t spectral_invert_toggle = 0; // toggles every interrupt togive a reference for spectral inversion
uint8_t kill_sample_toggle = 0; // toggles every other interrupt to kill every other sample


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



void PIT0_IRQHandler(void){	//This function is called when the timer interrupt expires
	//Place Interrupt Service Routine Here
	NVIC_ClearPendingIRQ(PIT0_IRQn);							//Clears interrupt flag in NVIC Register
	PIT->CHANNEL[0].TFLG	= PIT_TFLG_TIF_MASK;		//Clears interrupt flag in PIT Register		
	
	/* ----------  ADC READ ---------- */
	adc_measurement = ADC0->R[0]; // read conversion result
	ADC0->SC1[0] = ADC_SC1_ADCH(0); // set flag to start ADC conversion	
	/* ----------- DAC WRITE --------- */

	// ---------------- Problem 1 logic ----------------
	//spectral inversion logic 

	// if (spectral_invert_toggle) {
	// 	adc_measurement = 2048 - (adc_measurement - 2048); // invert around mid scale
	// }
	// spectral_invert_toggle = !spectral_invert_toggle; // toggle spectral inversion flag
	
	// ---------------- Problem 2 logic ----------------

	// kill every other sample while applying spectral inversion logic to the ones that stay
	if (kill_sample_toggle) {
		adc_measurement = DAC_MID; // set to mid scale to "kill" sample
	}
	if (spectral_invert_toggle) {
		adc_measurement = 2048 - (adc_measurement - 2048); // invert around mid scale
	}
	kill_sample_toggle = !kill_sample_toggle; // toggle kill sample flag
	spectral_invert_toggle = kill_sample_toggle ? spectral_invert_toggle : !spectral_invert_toggle; // toggle spectral inversion only when not killing sample

	// --------------- Problem 3 logic ----------------`
	//adjustable resolution digital wire 
	//mess with 12-bit DAC resolution by AND masking upper bits with 1 and lower bits with 0
	//adc_measurement = adc_measurement & dac_bitmask; // keep upper 6 bits -> 6-bit resolution




	/* output to dac */ 
	DAC_SetRaw(adc_measurement);
	/* ------------------------------- */
	
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
		// if (SW2_Pressed()){
		// 	//SW2 pressed
		// 	PTA->PCOR = (1u<<1);
		// }else{
		// 	//SW2 not pressed
		// 	PTA->PSOR = (1u<<1);
		// }
		// LED_cycle();
		// for (uint16_t v = 0; v <= 4095u; ++v){
		// 	DAC_SetRaw(v);
		// 	delay(10000u); // ~pause so you can observe each voltage
		// }
	}
}
