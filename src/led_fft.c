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
audio input at ADC10 port 4.

ADC10, TimerA interrupt LPM wakeup, TimerA PWM like output, button use, integer arithmatic
are used and demonstrated.

Features:

	. 8 bit integer FFT
	. 32 samples at 250Hz separation
	. shows 16 amplitudes of 250Hz, 500Hz, 750Hz,....... 5.75kHz, 6.75kHz, 7.75kHz non-linear
	. partial logarithm map to show amplitudes, limited as resolution has been reduced for 8 bit FFT
	. LM358 two stage mic pre-amp at 100x 100x gain (can be improve vastly w/ better op-amps)
	. draws power from launchpad
	. square signal generator from TA0.1 toggling, good for initial testing
	. TA0.1 ouput to P1.6 (io)
	. P1.3 button used to cycle thru 1. no ouput, 2. P1.6 signal
	. P2.3 button used to toggle LSB/_USB display mode: for LSB audio spectrum is reversed to mimic spectrum reversal in LSB RF modulation



          TI LaunchPad + Educational BoosterPack
         ---------------
     /|\|            XIN|-
      | |               |
      --|RST        XOUT|-
        |               |
        |           P1.4|<-- ADC4
				|               |
        |           P1.6|--> TA0.1
				|               |
				|           P1.5|--> SPI CLK
				|           P1.7|--> SPI MOSI
				|           P2.5|--> SPI /CS
				|               |
				|           P2.3|--> LSB/_USB switch

				#define LED_CS		BIT5					//2.5 is CS
				#define LED_DATA	BIT7					//1.7 is SPI MOSI
				#define LED_CLK		BIT5					//1.5 is SPI clock

				#define BUSY_PIN	BIT0					// P1.0 BUSY_PIN output


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
#include <string.h>
// sqrt: 150us
//#include <math.h>

#define OP_NOOP 0x00
#define OP_DECODEMODE 0x09
#define OP_INTENSITY 0x0A
#define OP_SCANLIMIT 0x0B
#define OP_SHUTDOWN 0x0C
#define OP_DISPLAYTEST 0x0F

#define LED_CS		BIT5					//2.5 is CS
#define LED_DATA	BIT7					//1.7 is SPI MOSI
#define LED_CLK		BIT5					//1.5 is SPI clock

#define BUSY_PIN	BIT0					// P1.0 BUSY_PIN output
#define RED_LED		BIT0
#define GREEN_LED	BIT6

#define SATURATION 16
#define DOTS 5
#define FILL 1
//#define DEBUG 1

typedef union
{
	unsigned long longs;
	unsigned int ints[2];
	unsigned char chars[4];
} longbytes;

union {
	unsigned char bytes[8*4];
	longbytes lbytes[8];
	unsigned long ulongs[8];
} dbuff;

//SPI initialization
void SPI_Init(void) {

	P1SEL |= LED_DATA + LED_CLK;				// spi init
	P1SEL2 |= LED_DATA + LED_CLK;				// spi init

	UCB0CTL1 = UCSWRST;
	UCB0CTL0 |= UCMSB + UCMST + UCSYNC + UCCKPH;		// 3-pin, 8-bit SPI master
	UCB0CTL1 |= UCSSEL_2;					// SMCLK
	UCB0BR0 = 2;						// spi speed is smclk/1 - 1MHz
	UCB0BR1 = 0;						//
	UCB0CTL1 &= ~UCSWRST;					// **Initialize USCI state machine**

	P2DIR |= LED_CS;					//cs is output
	P2SEL &= ~LED_CS;					//cs is not module
	P2SEL2 &= ~LED_CS;					//cs is not module
}

unsigned char spibuff[8];

void SPI_Write(unsigned char* array) {
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

void Init_MAX7219(void) {

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

void update_display() {
	unsigned char i;
	for(i = 0; i < 8; ++i) {
		spibuff[0] = spibuff[2] = spibuff[4] = spibuff[6] = i+1;
		spibuff[1] = dbuff.lbytes[i].chars[0];
		spibuff[3] = dbuff.lbytes[i].chars[1];
		spibuff[5] = dbuff.lbytes[i].chars[2];
		spibuff[7] = dbuff.lbytes[i].chars[3];
		SPI_Write(spibuff);
	}
}

// 100us
unsigned short sqrt32(unsigned long a) {
	unsigned long rem = 0, root = 0;
	int i;
	for(i = 0; i < 16; ++i) {
		root <<= 1;
		rem = ((rem << 2) + (a >> 30));
		a <<= 2;
		++root;

		if(root <= rem) {
			rem -= root;
			++root;
		} else {
			--root;
		}
	}
	return (unsigned short)(root >> 1);
}

// 50us
unsigned char sqrt16(unsigned short a) {
	unsigned short rem = 0, root = 0;
	int i;
	for(i = 0; i < 8; ++i) {
		root <<= 1;
		rem = ((rem << 2) + (a >> 14));
		a <<= 2;
		++root;

		if(root <= rem) {
			rem -= root;
			++root;
		} else {
			--root;
		}
	}
	return (unsigned char)(root >> 1);
}

//______________________________________________________________________
volatile uint16_t play_at = 0;
volatile uint16_t ticks=0;
uint16_t droop = 0;

// scilab 255 * window('kr',64,6)
//const unsigned short hamming[32] = { 4, 6, 9, 13, 17, 23, 29, 35, 43, 51, 60, 70, 80, 91, 102, 114, 126, 138, 151, 163, 175, 187, 198, 208, 218, 227, 234, 241, 247, 251, 253, 255 };
const unsigned short hamming[64] = { 4, 6, 9, 13, 17, 23, 29, 35, 43, 51, 60, 70, 80, 91, 102, 114, 126, 138, 151, 163, 175, 187, 198, 208, 218, 227, 234, 241, 247, 251, 253, 255, 255, 253, 251, 247, 241, 234, 227, 218, 208, 198, 187, 175, 163, 151, 138, 126, 114, 102, 91, 80, 70, 60, 51, 43, 35, 29, 23, 17, 13, 9, 6, 4 };
// scilab 255 * window('kr',64,4)
//const unsigned short hamming[32] = { 23, 29, 35, 42, 50, 58, 66, 75, 84, 94, 104, 113, 124, 134, 144, 154, 164, 174, 183, 192, 201, 210, 217, 224, 231, 237, 242, 246, 250, 252, 254, 255 };
//const unsigned short hamming[64] = { 23, 29, 35, 42, 50, 58, 66, 75, 84, 94, 104, 113, 124, 134, 144, 154, 164, 174, 183, 192, 201, 210, 217, 224, 231, 237, 242, 246, 250, 252, 254, 255, 255, 254, 252, 250, 246, 242, 237, 231, 224, 217, 210, 201, 192, 183, 174, 164, 154, 144, 134, 124, 113, 104, 94, 84, 75, 66, 58, 50, 42, 35, 29,23 };
// scilab 255 * window('kr',64,2)
//const unsigned short hamming[32] = { 112, 119, 126, 133, 140, 147, 154, 161, 167, 174, 180, 186, 192, 198, 204, 209, 214, 219, 224, 228, 232, 236, 239, 242, 245, 247, 250, 251, 253, 254, 255, 255 };
//const unsigned short hamming[64] = { 112, 119, 126, 133, 140, 147, 154, 161, 167, 174, 180, 186, 192, 198, 204, 209, 214, 219, 224, 228, 232, 236, 239, 242, 245, 247, 250, 251, 253, 254, 255, 255, 255, 255, 254, 253, 251, 250, 247, 245, 242, 239, 236, 232, 228, 224, 219, 214, 209, 204, 198, 192, 186, 180, 174, 167, 161, 154, 147, 140, 133, 126, 119, 112 };

int16_t fix_fft(int8_t fr[], int8_t fi[], int16_t m, int16_t inverse);
//int16_t fix_fft(int16_t fr[], int16_t fi[], int16_t m, short inverse);
//______________________________________________________________________
int main(void) {

	WDTCTL = WDTPW + WDTHOLD;				// Stop WDT
	BCSCTL1 = CALBC1_16MHZ;					// 16MHz clock
	DCOCTL = CALDCO_16MHZ;

	P1SEL = P2SEL = 0;
	P1DIR = P2DIR = 0;
	P1OUT = P2OUT = 0;

	P1DIR |= BUSY_PIN;
	P1OUT &= ~BUSY_PIN;

	//______________ led port use
	SPI_Init();
	__delay_cycles(100000);
	Init_MAX7219();
	__delay_cycles(1000);

	//______________ adc setting, use via microphone jumper on educational boost
	ADC10CTL0 = SREF_0 + ADC10SHT_2 + REFON + ADC10ON + ADC10IE;
//	ADC10CTL0 = SREF_0 + ADC10SHT_2 + ADC10ON + ADC10IE;
	ADC10CTL1 = INCH_4;					// input A4
	ADC10AE0 |= BIT4;					// P1.4 ADC microphone

	uint8_t gen_tone = 0;					// default, not tone generation

	P1OUT |= BIT3;						// tactile button pull-up
	P1REN |= BIT3;

	P2OUT |= BIT3 | BIT4;
	P2REN |= BIT3 | BIT4;

	//______________ setup test tone signal via TA0.1
	TA0CCR0 = TA0CCR1 = 0;

	TA0CTL = TASSEL_2 + MC_2 + TAIE;			// smclk, continous mode
	TA0CCTL1 = OUTMOD_4 + CCIE;				// we want pin-toggle, 2 times slower
	TA0CCR1 = play_at;
	P1DIR |= BIT6;						// prepare both T0.1
	_BIS_SR(GIE); 						// now

	uint8_t i=0,j=0;

#define log2FFT   5
#define FFT_SIZE  (1<<log2FFT)
#define Nx	(2 * FFT_SIZE)
#define log2N     (log2FFT + 1)
//#define BAND_FREQ_KHZ	8
#define BAND_FREQ_KHZ	4

	for (i=0;i<8;i++)
		dbuff.ulongs[i] = i; //0UL;
	update_display();

	int16_t offset;
	int8_t data[Nx], im[Nx];
	int16_t sample[Nx];
	uint8_t plot[Nx/2];
	bzero(plot, Nx/2);
	uint8_t cnt=0, freq=0;
	while (1) {
		if (gen_tone) {
			if (!(++cnt&0x7f)) {
				cnt = 0;
				freq++;
				if (freq > 31)
					freq = 1;
				//____________ now play at 250Hz increments
				//play_at = (16000/freq*2)-1;
				//____________ now play at 125Hz increments
				//play_at = (16000/freq*4)-1;
				play_at = (16000/freq*(16/BAND_FREQ_KHZ))-1;
				__delay_cycles(100000);
			}//if
		}//if

		bzero(im, Nx);
		offset = 0;

#ifdef SATURATION
		P1OUT &= ~BUSY_PIN;
#endif // SATURATION

		TA0CCR0 = TA0R;
		TA0CCTL0 |= CCIE;
		for (i=0;i<Nx;i++) {

			// time delay between adc samples
			// this will become the band frequency after time - frequency conversion

			TA0CCR0 += (16000/(BAND_FREQ_KHZ*2))-1;	// begin counting for next period
			ADC10CTL0 |= ENC + ADC10SC;		// sampling and conversion start
			while (ADC10CTL1 & ADC10BUSY);		// stay and wait for it

			sample[i] = ADC10MEM - 512 + 8; //>>2) - 128;		// signal leveling?
//			offset += data[i];
			offset += sample[i];
//			data[i] = (ADC10MEM>>2) - 128;		// signal leveling?
//			hamm = (ADC10MEM>>2) - 128;		// signal leveling?
//			hamm *= hamming[i<31?i:63-i];
//			data[i] = hamm / 256;
//			data[i] = hamm >> 8;
			//data[i] = ADC10MEM - 512;		// signal leveling?
			//im[i] = 0;

			_BIS_SR(LPM0_bits + GIE);		// wake me up when timeup

#ifdef SATURATION
			// turn on LED if saturation detected
			if((sample[i] > (1023 - SATURATION)) || (sample[i] < SATURATION))
				P1OUT |= BUSY_PIN;
#endif // SATURATION

		}//for
		TA0CCTL0 &= ~CCIE;

		//offset >>= (log2FFT+1);
		offset /= Nx;
		//offset /= 16;
		//offset /= 4;
		// signal leveling
		for (i=0;i<Nx;i++)
		{
			//data[i] -= offset >> (log2FFT+1);
			sample[i] -= offset;
			sample[i] >>= 2;
			//data[i] = (sample[i] - offset) >> 2;
			data[i] = (uint8_t)sample[i];
		}

		// pseudo oscilloscope
		if (P2IN&BIT4) {

//			switch(gen_tone) {
//				case 0:
//					data[i] -= offset >> (log2FFT+1);
//				break;
//				default:
//					data[i] -= 128;
//				break;
//			}

//#define WINDOWING
#ifdef WINDOWING
				int hamm;
				hamm = 0;
	//		if(gen_tone==0) {
				// windowing
				for (i=0;i<Nx;i++) {
					hamm = hamming[i] * data[i];
					//hamm = hamming[i<(FFT_SIZE-1)?i:(Nx-1)-i] * data[i];
	//				data[i] = (offset >> (log2FFT+1)) + (hamm >> 8);
					data[i] = (hamm >> 8);
				}
	//		}
#endif // WINDOWING

			P1OUT |= BUSY_PIN;
			fix_fft(data, im, log2N, 0);	// thank you, Tom Roberts(89),Malcolm Slaney(94),...
			P1OUT &= ~BUSY_PIN;

			for (i=0;i<FFT_SIZE;i++) {
				//P1OUT |= BUSY_PIN;
				data[i] = sqrt16(data[i]*data[i] + im[i]*im[i]);
				//P1OUT &= ~BUSY_PIN;
				//
				if (gen_tone) {
					data[i] >>= 2;
					data[i] -= data[i] >> 1;
				} else {
					//_______ logarithm scale mapping
					//const uint16_t lvls[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 16, 22, 32, 45, 63, 89, 65535 };
					//const uint16_t lvls[] = { 1, 2, 3, 4, 5, 12, 34, 94, 65535 };
	//                                  0  1  2  3  4   5   6   7
					const uint16_t lvls[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 65535 };
					//const uint16_t lvls[] = { 0, 2, 6, 8, 10, 65535 };
					//const uint16_t lvls[] = { 0, 1, 2, 4, 8, 16, 32, 64, 65535 };
					//const uint16_t lvls[] = { 0, 2, 6, 12, 30, 80, 128, 65535 };
					//const uint16_t lvls[] = { 0, 1, 3, 6, 10, 20, 48, 84, 65535 };
					//const uint16_t lvls[] = { 1, 2, 3, 4, 5, 6, 12, 24, 65535 };
					uint8_t c = 0; //sizeof(lvls)/sizeof(uint16_t);
					while(lvls[c] < data[i])
				 		(++c);
					data[i] = c;
				}

				if (data[i] > 9)
					data[i] = 8;
				if (data[i] < 0)
					data[i] = 0;
				if (data[i] > plot[i])
				{
					plot[i] = data[i];
				} else {
#ifdef DOTS
					if(!droop && plot[i])
						plot[i]--;
#endif // DOTS
				}//else

			}//for

#ifdef DOTS
			if(droop)
				--droop;
			else
				droop = DOTS;
#endif // DOTS

			unsigned long mask = 1UL, rmask = 1UL << 31;;
			for(i = 0; i < FFT_SIZE; ++i, mask <<= 1, rmask >>= 1) {
#ifdef FILL
				for(j = 0; j<8; ++j)
				{
					//if(j<plot[i])
					if(j<data[i])
						dbuff.ulongs[j] |= (P2IN&BIT3)?rmask:mask;
					else
						dbuff.ulongs[j] &= ~((P2IN&BIT3)?rmask:mask);
				}
#endif
#ifdef DOTS
				dbuff.ulongs[plot[i]] |= (P2IN&BIT3)?rmask:mask;
				//dbuff.ulongs[plot[i]] |= mask;
#endif
			}//for

#ifdef DEBUG
			//dbuff.lbytes[7].chars[0] = freq;
			//dbuff.lbytes[7].chars[freq>15?0:3] = freq;
			//dbuff.lbytes[7].ints[1] = offset; // >> (log2FFT+1));
			if(abs(offset) < 15)
				dbuff.ulongs[7] |= 1UL << (offset + 16);
			else
				dbuff.lbytes[7].ints[1] |= offset;
//			if (gen_tone)
//				dbuff.lbytes[7].chars[freq>15?0:3] = freq;
//			else
//				dbuff.lbytes[7].ints[1] = offset; // >> (log2FFT+1));

		//dbuff.lbytes[7].ints[0] = offset; // >> (log2FFT+1));
#endif

		// pseudo-scilloscope
		} else {

#define LEVELING
#ifdef LEVELING
			// signal leveling
			switch (gen_tone) {
				case 1:
					for (i=0;i<Nx;i++)
						data[i] -= 128;
				break;
				case 2:
					for (i=0;i<Nx;i++)
						data[i] -= offset >> (log2FFT+1);
				break;
			}//switch
#endif //def LEVELING

			// 0..63
			for (i=0;i<Nx;i++) {
				for(j=0;j<8;++j) {
					if((0x07 & ((data[i]
#ifdef LEVELING
								 + ((gen_tone == 1)?128:0)
#endif //def LEVELING
									) >> 5)) == j) // j <2^3> == data <2^8>
						dbuff.ulongs[j] |= 1UL << (i/2);
					else
						dbuff.ulongs[j] &= ~(1UL << (i/2));
				}//for
			}//for
		}

		if (!(P1IN&BIT3)) {
			while (!(P1IN&BIT3)) asm("nop");
			play_at = 0;
			P1SEL &= ~BIT6;
			gen_tone++;
			switch (gen_tone) {
				case 1:
					P1SEL |= BIT6;		// pin toggle on
					ADC10CTL0 &= ~ENC;
					ADC10CTL0 &= ~(SREF0 | SREF1 | SREF2);
					ADC10CTL0 |= SREF_1 | ENC;
					break;
				default:
					gen_tone = 0;
					ADC10CTL0 &= ~ENC;
					ADC10CTL0 &= ~(SREF0 | SREF1 | SREF2);
					ADC10CTL0 |= SREF_0 | ENC;
					break;
			}//switch
		}//if

		//P1OUT |= BUSY_PIN;
		update_display();
		//P1OUT &= ~BUSY_PIN;
		bzero(dbuff.ulongs, 8*4);

		//__delay_cycles(100000);			// personal taste
		if (!gen_tone) {
			i=7; //25;
			while(--i)
				__delay_cycles(65535);			// personal taste
		}
	}//while

}

// ADC10 interrupt service routine
#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(ADC10_VECTOR))) ADC10_ISR (void)
#else
#error Compiler not supported!
#endif
{
	__bic_SR_register_on_exit(CPUOFF);
}

#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer0_A0_iSR(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(TIMER0_A0_VECTOR))) Timer0_A0_iSR (void)
#else
#error Compiler not supported!
#endif
{
	//P1OUT ^= BIT0;
	__bic_SR_register_on_exit(CPUOFF);
}

//________________________________________________________________________________
//interrupt(TIMERA1_VECTOR) Timer_A1(void) {
#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=TIMER0_A1_VECTOR
__interrupt void Timer0_A1_iSR(void)
#elif defined(__GNUC__)
void __attribute__ ((interrupt(TIMER0_A1_VECTOR))) Timer0_A1_iSR (void)
#else
#error Compiler not supported!
#endif
{
	switch(TAIV) {
		case TA0IV_TACCR1:
			CCR1 += play_at;
			break;
		case TA0IV_TAIFG:
			if (ticks)
				ticks--;
			break;
	}//switch
}
//
