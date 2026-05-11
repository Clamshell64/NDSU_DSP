/**************************************************************************/
//Name:  TimerInt.h																												//
//Purpose:  Allow configuration and use of Timer Interrupts								//
//Author:  Ethan Hettwer																									//
//Revision:  1.1 12Sept2014 EH Convert to K22f														//
//					 1.0 4Sept2014 EH Initial Revision														//
//Target:  Freescale K22f																									//
/**************************************************************************/

#ifndef __TIMERINT_H_
#define __TIMERINT_H_

#define INTERRUPT_CLOCKS 1361u // 60,000,000 clocks/sec / 1361 clocks/interrupt -> 44,100 interrupts/sec (44.1kHz)
void TimerInt_Init(void); 											//Initialize Timer Interrupt

#endif
