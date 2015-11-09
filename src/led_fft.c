/******************************************************************************
CREDITS:

The core fft code is a "fix_fft.c" which have been floating around on the web
for many years. The particular version that I eventually adopted has the following
footprints;

  Written by:  Tom Roberts  11/8/89
  Made portable:  Malcolm Slaney 12/15/94 malcolm@interval.com
  Enhanced:  Dimitrios P. Bouras  14 Jun 2006 dbouras@ieee.org
  Modified for 8bit values David Keller  10.10.2010


Description:

This audio spectrum analyzer is a project for the TI Launchpad (Value Line) w/
CircuitCo's Educational BoosterPack. It is microphone based and require minimal
external components. Efforts were made to maximize the use of device / features from
the Educational BoosterPack.

ADC10, TimerA interrupt LPM wakeup, TimerA PWM like output, button use, integer arithmatic
are used and demonstrated.

Features:

	. 8 bit integer FFT
	. 32 samples at 250Hz separation
	. shows 16 amplitudes of 250Hz, 500Hz, 750Hz,....... 5.75Khz, 6.75Khz, 7.75Khz non-linear
	. partial logarithm map to show amplitudes, limited as resolution has been reduced for 8 bit FFT
	. LM358 two stage mic pre-amp at 100x 100x gain (can be improve vastly w/ better op-amps)
	. utilize Educational BoosterPack; mic for input, potentiometer for pre-amp biasing
	. draws power from launchpad
	. square signal generator from TA0.1 toggling, good for initial testing
	. TA0.1 ouput to P1.6 (io) or P2.6 (buzzer)
	. P1.3 button used to cycle thru 1. no ouput, 2. P1.6 signal, 3. P2.6 buzzer
	* in mode 2 and 3, both band and amplitude scales are linear
	* in mode 3, signals are distorted after passing buzzer and condensor mic, especially in low frequency


          TI LaunchPad + Educational BoosterPack
         ---------------
     /|\|            XIN|-
      | |               |
      --|RST        XOUT|-
        |               |
        |           P1.4|<-- ADC4
        |               |
        |           P1.6|--> TA0.1



   LM358 Dual Op-Amp, GBW @0.7Mhz, each stage @x100 gain, bandwidth is 7Khz

                     +------------------------------+
                    _|_                             |
                    ___ 10uF                        |
                   + |   ---------------            |
                     +-1|----.       Vcc|8          |
                     |. |    ^      .---|7--+-------|-----o (A)
                100k| | |   / \     ^   |   |.      |.
                    |_| |  /__+\   / \  |  | |100k | |1k
       0.1u          |  |   | |   /__+\ |  |_|     |_|
 (B) o--||--[ 1k  ]--+-2|---+ |    | |  |   |       |
 (C) o-------------+---3|-----+    +-|--|6--+-------+
                   |   4|Gnd         +--|5----+
                   |     ---------------      |
                   |                          |
                   +--------------------------+

  (A) to P1.4 EduBoost Mic jumper middle pin
  (B) to Condenser Mic, EduBoost Mic Jumper top pin
  (C) to Potentiometer, EduBooster Potentiometer Jumper top pin
  (*) connect Gnd + Vcc to Launchpad


 Chris Chung June 2013
 . init release

 code provided as is, no warranty

 you cannot use code for commercial purpose w/o my permission
 nice if you give credit, mention my site if you adopt much of my code


******************************************************************************/

#include <msp430.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define OP_NOOP 0x00
#define OP_DECODEMODE 0x09
#define OP_INTENSITY 0x0A
#define OP_SCANLIMIT 0x0B
#define OP_SHUTDOWN 0x0C
#define OP_DISPLAYTEST 0x0F

#define LED_CS		BIT5	//2.5 is CS
#define LED_DATA	BIT7	//1.7 is SPI MOSI
#define LED_CLK		BIT5	//1.5 is SPI clock

typedef union
{
	unsigned long longn;
	unsigned int ints[2];
	unsigned char chars[4];
} longbytes;

union {
	unsigned char bytes[8*4];
	longbytes lbytes[8];
	unsigned long ulongs[8];
} dbuff;

void SPI_Init(void) //SPI initialization
{
	P1SEL |= LED_DATA + LED_CLK;			//spi init
	P1SEL2 |= LED_DATA + LED_CLK;			//spi init

	UCB0CTL1 = UCSWRST;
	UCB0CTL0 |= UCMSB + UCMST + UCSYNC + UCCKPH;	// 3-pin, 8-bit SPI master
	UCB0CTL1 |= UCSSEL_2;				// SMCLK
	UCB0BR0 = 0;					// spi speed is smclk/1 - 1MHz
	UCB0BR1 = 0;					//
	UCB0CTL1 &= ~UCSWRST;				// **Initialize USCI state machine**

	P2DIR |= LED_CS;				//cs is output
	P2SEL &= ~LED_CS;				//cs is not module
	P2SEL2 &= ~LED_CS;				//cs is not module
}

unsigned char spibuff[8];

void SPI_Write(unsigned char* array)
{
	P2OUT &= ~LED_CS;
	__delay_cycles(50);
	unsigned int h = 8;
	while(h) {
		UCB0TXBUF = *array;
		while (UCB0STAT & UCBUSY);
		++array; --h;
	}
	P2OUT |= LED_CS;
}

void Init_MAX7219(void)
{
	unsigned char config_reg[5] = { OP_DECODEMODE, OP_INTENSITY, OP_SCANLIMIT, OP_SHUTDOWN, OP_DISPLAYTEST };
	unsigned char config_val[5] = {          0x00,         0x00,         0x07,        0x01,           0x00 };

	unsigned int g,h;

	for(h = 0; h < sizeof(config_reg); ++h) {
		for(g = 0; g < 4; ++g) {
			spibuff[ (g << 1)     ] = config_reg[h];
			spibuff[ (g << 1) + 1 ] = config_val[h];
		}
		SPI_Write(spibuff);
	};
}

void update_display()
{
	unsigned char i;
	for(i = 0; i < 8; ++i) {
		spibuff[0] = i+1;
		spibuff[1] = dbuff.lbytes[i].chars[0];
		spibuff[2] = i+1;
		spibuff[3] = dbuff.lbytes[i].chars[1];
		spibuff[4] = i+1;
		spibuff[5] = dbuff.lbytes[i].chars[2];
		spibuff[6] = i+1;
		spibuff[7] = dbuff.lbytes[i].chars[3];

		SPI_Write(spibuff);
	}
}

uint16_t int_sqrt32(uint32_t x)
{
	uint16_t res=0;
	uint16_t add= 0x8000;
	int i;
	for(i=0;i<16;i++) {
		uint16_t temp=res | add;
		uint32_t g2=temp*temp;
		if (x>=g2) {
			res=temp;
		}
		add>>=1;
	}
	return res;
}

//______________________________________________________________________
volatile uint16_t play_at = 0;
volatile uint8_t ticks=0;

int16_t fix_fft(int8_t fr[], int8_t fi[], int16_t m, int16_t inverse);
//______________________________________________________________________
int main(void) {

	WDTCTL = WDTPW + WDTHOLD;		// Stop WDT
	BCSCTL1 = CALBC1_16MHZ;			// 16MHz clock
	DCOCTL = CALDCO_16MHZ;

	P1SEL = P2SEL = 0;
	P1DIR = P2DIR = 0;
	P1OUT = P2OUT = 0;

	//______________ led port use
	SPI_Init();

	__delay_cycles(100000);
	Init_MAX7219();
	__delay_cycles(1000);

	//______________ adc setting, use via microphone jumper on educational boost
	ADC10CTL0 = SREF_1 + ADC10SHT_2 + REFON + ADC10ON + ADC10IE;
	ADC10CTL1 = INCH_4;		       // input A4
	ADC10AE0 |= BIT4;			 // P1.4 ADC microphone

	uint8_t gen_tone=0;						// default, not tone generation

	P1OUT |= BIT3;			// tactile button pull-up
	P1REN |= BIT3;
	//______________ setup test tone signal via TA0.1
	TA0CCR0 = TA0CCR1 = 0;

	TA0CTL = TASSEL_2 + MC_2 + TAIE;	// smclk, continous mode
	TA0CCTL1 = OUTMOD_4 + CCIE;			// we want pin-toggle, 2 times slower
	TA0CCR1 = play_at;
	P1DIR |= BIT6;		// prepare both T0.1
	P2DIR |= BIT6;
	_BIS_SR(GIE); 						// now

	uint8_t i=0,j=0;

#define log2FFT   5
#define FFT_SIZE  (1<<log2FFT)
#define Nx	(2 * FFT_SIZE)
#define log2N     (log2FFT + 1)
#define BAND_FREQ_KHZ	8


	const  uint8_t pick[16] = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 18, 20, 23, 27, 31, };
	int8_t data[Nx], im[Nx];
	uint8_t plot[Nx/2]; // = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };
	uint8_t cnt=0, freq=0;
	while (1) {
		if (gen_tone) {
			if (!(++cnt&0x3f)) {
				cnt = 0;
				freq++;
				if (freq > 16) freq = 1;
				//____________ now play at 250Hz increments
				play_at = (16000/freq*2)-1;
				P1OUT ^= BIT0;
				__delay_cycles(100000);
				P1OUT ^= BIT0;
			}//if
		}//if

		TA0CCR0 = TA0R;
		TA0CCTL0 |= CCIE;
		for (i=0;i<Nx;i++) {

			// time delay between adc samples
			// this will become the band frequency after time - frequency conversion

			TA0CCR0 += (16000/(BAND_FREQ_KHZ*2))-1;	// begin counting for next period
			ADC10CTL0 |= ENC + ADC10SC;			// sampling and conversion start
			while (ADC10CTL1 & ADC10BUSY);		// stay and wait for it

			data[i] = (ADC10MEM>>2) - 128;		// signal leveling?
			//data[i] = ADC10MEM - 512;		// signal leveling?
			im[i] = 0;

			_BIS_SR(LPM0_bits + GIE);			// wake me up when timeup

		}//for
		TA0CCTL0 &= ~CCIE;

		fix_fft(data, im, log2N, 0);	// thank you, Tom Roberts(89),Malcolm Slaney(94),...

		//_______ logarithm scale mapping
		const uint16_t lvls[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 16, 22, 32, 45, 63, 89,  };

		for (i=0;i<FFT_SIZE;i++) {
			data[i] = sqrt(data[i]*data[i] + im[i]*im[i]);
			//
			if (gen_tone) {
				data[i] >>= 3;
			}//if
			else {
				uint8_t c=16;
				while (--c) {
					if (data[i] >= lvls[c]) {
						data[i] = c;
						break;
					}//if
				}//while
				if (!c) data[i] = 0;
			}//else
			if (data[i] > 16) data[i] = 15;
			if (data[i] < 0) data[i] = 0;
			if (data[i] > plot[i]) {
				plot[i] = data[i];
			}//if
			else {
				if (plot[i])
					plot[i]--;
			}//else
			// debug use
			//eblcd_hex(data[i]>>8);
			//eblcd_hex(data[i]&0xff);
		}//for

		for (i=0;i<16;i++) {
			uint8_t idx = pick[i];
			if (gen_tone) idx = i;
			for(j=0;j<8;++j) {
				if(j<plot[idx]) {
					dbuff.ulongs[j] |= 1UL << i;
				} else {
					dbuff.ulongs[j] &= ~(1UL << i);
				}
			}
		}//for

		if (gen_tone) {
			dbuff.lbytes[0].chars[3] = freq;
		}//if
		if (!(P1IN&BIT3)) {
			while (!(P1IN&BIT3)) asm("nop");
			play_at = 0;
			P1SEL &= ~BIT6;
			P2SEL &= ~BIT6;
			gen_tone++;
			switch (gen_tone) {
				case 1:
					P1SEL |= BIT6;	// pin toggle on
					break;
				case 2:
					P2SEL |= BIT6;	// buzzer on
					break;
				default:
					gen_tone = 0;
					break;
			}//switch
		}//while

		update_display();
		//__delay_cycles(100000);		// personal taste
	}//while

}

// ADC10 interrupt service routine
#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR (void) {
	__bic_SR_register_on_exit(CPUOFF);
}

// Timer A0 interrupt service routine
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer0_A0 (void) {
	//P1OUT ^= BIT0;
	__bic_SR_register_on_exit(CPUOFF);
}

//________________________________________________________________________________
//interrupt(TIMERA1_VECTOR) Timer_A1(void) {
#pragma vector=TIMER0_A1_VECTOR
__interrupt void Timer0_A1 (void) {
	switch(TAIV) {
		case  2:
			CCR1 += play_at;
			break;
		case 10:
			if (ticks) ticks--;
			break;
	}//switch
}
//
