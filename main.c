#include "mcu_regs.h"
#include "type.h"
#include "uart.h"
#include "stdio.h"
#include "timer32.h"
#include "gpio.h"
#include "i2c.h"
#include "ssp.h"
#include "adc.h"
#include "system_LPC13xx.h"
#include "light.h"
#include "joystick.h"
#include "pca9532.h"
#include "acc.h"
#include "rotary.h"
#include "led7seg.h"
#include "oled.h"
#include "rgb.h"

#include <stdlib.h>

#define TIMER_RESET     _BIT(1)
#define TIMER_RUN       _BIT(0)
#define MR0_I           _BIT(0)
#define MR0_R           _BIT(1)
#define MR0_S           _BIT(2)
#define MR1_I           _BIT(3)
#define MR1_R           _BIT(4)
#define MR1_S           _BIT(5)
#define MR2_I           _BIT(6)
#define MR2_R           _BIT(7)
#define MR2_S           _BIT(8)
#define MR3_I           _BIT(9)
#define MR3_R           _BIT(10)
#define MR3_S           _BIT(11)

#define TIMER_MR0_INT        _BIT(0)
#define TIMER_MR1_INT        _BIT(1)
#define TIMER_MR2_INT        _BIT(2)
#define TIMER_MR3_INT        _BIT(3)
#define TIMER_CR0_INT        _BIT(4)
#define TIMER_CR1_INT        _BIT(5)
#define TIMER_CR2_INT        _BIT(6)
#define TIMER_CR3_INT        _BIT(7)
#define TIMER_ALL_INT        (TIMER_MR0_INT | TIMER_MR1_INT | TIMER_MR2_INT | TIMER_MR3_INT | TIMER_CR0_INT | TIMER_CR1_INT | TIMER_CR2_INT | TIMER_CR3_INT)

uint8_t is_play_song = TRUE;

#define PAUSE 2
#define BROKEN 1
#define GOOD 0
#define STOPJOYSTICK 100

uint32_t joystick_status=0;
uint8_t acc_status = GOOD;

static void putSquare(uint8_t x, uint8_t y, oled_color_t color) {
	x *= 3;
	y *= 3;
	y++;
	for (uint8_t i = 0; i < 2; i++) {
		for (uint8_t j = 0; j < 2; j++) {
			oled_putPixel(x + j, y + i, color);
		}
	}
}

void blinkLEDs (uint8_t ctr) {
	static uint8_t counter;

	if (ctr != 0) {
		counter = ctr;
		return;
	} else if (counter == 0) {
		return;
	}

	if (counter == 1) {
		pca9532_setLeds(0x0000, 0xffff);
	} else if (counter%2) {
		pca9532_setLeds(0xff00, 0x00ff);
	} else {
		pca9532_setLeds(0x00ff, 0xff00);
	}

	counter -= 1;
}

typedef enum{CENTER, UP, DOWN, RIGHT, LEFT} directions;
#define maxSnakeLength 255

/*****************************************************************************
 ** Function name:                processSnake
 **
 ** Descriptions:                init and move snake, blink and eat dots,
 **                      requires srand() to be called earlier
 **
 ** parameters:                        state of joystick and accelerometer
 **
 ** Returned value:                None
 *****************************************************************************/
#define LEVEL_WIDTH (uint8_t)(OLED_DISPLAY_WIDTH / 3)
#define LEVEL_HEIGHT (uint8_t)(OLED_DISPLAY_HEIGHT / 3)
static void processSnake (uint8_t controlsState) {
	static uint8_t virgin = TRUE; //is set to FALSE after first call
	static uint8_t dir = 0;

	typedef struct Vector2 {
		int16_t x;
		int16_t y;
	} Vector2;

	static uint8_t hasSnake[LEVEL_HEIGHT][LEVEL_WIDTH] = {};

	static const Vector2 direction[4] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}}; //up, down, left, right
	static Vector2 snake[maxSnakeLength] = {};
	static uint16_t currentHead = 0;
	static uint16_t currentLength = 5;
	static Vector2 dot;
	static uint8_t dotBlink = 0;

	//this will hold current light sensor readings
	//reading light sensor readings
	uint32_t lux = light_read();

	//at first call create snake and put dot
	if (virgin) {
		virgin = FALSE;
		for (int i = currentLength; i>0; i--) {
			snake[i].x = LEVEL_WIDTH / 2;
			snake[i].y = LEVEL_HEIGHT / 2;
		}
		dot.x = rand() % LEVEL_WIDTH;
		dot.y = rand() % LEVEL_HEIGHT;
	}

	//get dir index from controlsState
	if (controlsState & JOYSTICK_UP) {
		if (dir != 1)
			dir = 0;
	} else if (controlsState & JOYSTICK_DOWN) {
		if (dir != 0)
			dir = 1;
	} else if (controlsState & JOYSTICK_LEFT) {
		if (dir != 3)
			dir = 2;
	} else if (controlsState & JOYSTICK_RIGHT) {
		if (dir != 2)
			dir = 3;
	}

	//erase tail
	uint16_t tail = (currentHead + 1) % currentLength;
	putSquare(snake[tail].x, snake[tail].y, OLED_COLOR_BLACK);
	hasSnake[snake[tail].y][snake[tail].x] = FALSE;

	//make head
	snake[tail].x = snake[currentHead].x + direction[dir].x;
	snake[tail].y = snake[currentHead].y + direction[dir].y;
	currentHead = tail;

	//wind snake around screen
	if (snake[currentHead].x < 0)
		snake[currentHead].x = LEVEL_WIDTH - 1;
	if (snake[currentHead].y < 0)
		snake[currentHead].y = LEVEL_HEIGHT - 1;
	if (snake[currentHead].x == LEVEL_WIDTH)
		snake[currentHead].x = 0;
	if (snake[currentHead].y == LEVEL_HEIGHT)
		snake[currentHead].y = 0;

	//calculate head to dot distance
	int16_t xHeadToDot = snake[currentHead].x - dot.x;
	if (xHeadToDot < 0)
		xHeadToDot *= -1;
	int16_t yHeadToDot = snake[currentHead].y - dot.y;
	if (yHeadToDot < 0)
		yHeadToDot *= -1;

	//check if head is close enough to dot
	const int TOLERANCE=0;
	if (xHeadToDot <= TOLERANCE && yHeadToDot <= TOLERANCE) {
		//extend snake length by 1
		uint16_t newTail = (currentHead + 1) % currentLength;
		snake[currentLength].x = snake[newTail].x;
		snake[currentLength].y = snake[newTail].y;
		currentLength++;
		blinkLEDs(7);
		//play song
		if (currentLength%5 == 0) {
			is_play_song = TRUE;
		}
		//put new dot
		putSquare(dot.x, dot.y, OLED_COLOR_BLACK);
		dot.x = rand() % LEVEL_WIDTH;
		dot.y = rand() % LEVEL_HEIGHT;
	} else {
		//blink dot
		putSquare(dot.x, dot.y, (oled_color_t)(dotBlink ^= 1));
	}

	if (hasSnake[snake[currentHead].y][snake[currentHead].x] || lux < 100) {
		//die
		virgin = TRUE;
		dir = 0;
		for (int i = 0; i < currentLength; i++) {
			putSquare(snake[i].x, snake[i].y, OLED_COLOR_BLACK);
			hasSnake[snake[i].y][snake[i].x] = FALSE;
		}
		currentHead = 0;
		currentLength = 5;
		dotBlink = 0;
		putSquare(dot.x, dot.y, OLED_COLOR_BLACK);
	} else {
		//put head
		putSquare(snake[currentHead].x, snake[currentHead].y, OLED_COLOR_WHITE);
		hasSnake[snake[currentHead].y][snake[currentHead].x] = TRUE;
	}

}

//pitch in Hz
static uint32_t notes[] = {
		440,
		494,
		262,
		294,
		330,
		349,
		392,
		880,
		988,
		523,
		587,
		659,
		698,
		784
};

#define P1_2_HIGH() (LPC_GPIO1->DATA |= (0x1<<2))
#define P1_2_LOW()  (LPC_GPIO1->DATA &= ~(0x1<<2))


/******************************************************************
 ** Function name:                getNote
 **
 ** Descriptions:                convert letter name of note into pitch
 **
 ** parameters:                        single character descripting note
 **
 ** Returned value:                pitch
 ******************************************************************/
static uint32_t getNote (char ch) {
	const int8_t position_of_a = 7;

	if (ch >= 'A' && ch <= 'G') {
		return notes[ch - 'A'];
	}
	if (ch >= 'a' && ch <= 'g') {
		return notes[ch - 'a' + position_of_a];
	}
	return 0;
}

/************************************************************************
 ** Function name:                getDuration
 **
 ** Descriptions:                get duration of note on scale from 0 to 2000 ms
 **
 ** parameters:                        char descripting duration from '0' to '9'
 **
 ** Returned value:                duration in miliseconds
 ************************************************************************/
static uint32_t getDuration (char ch) {
	if (ch < '0' || ch > '9') {
		return 0;
	}

	return (ch - '0') * 200;
}

/************************************************************************
 ** Function name:                getPause
 **
 ** Descriptions:                get duration of pause between notes in ms
 **                      described as special characters
 **
 ** parameters:                        char descripting pause: '+', ',', '.' or '_'
 **
 ** Returned value:                duration in miliseconds
 ************************************************************************/
static uint32_t getPause (char ch) {
	switch (ch) {
	case '+':
		return 0;
	case '.':
		return 5;
	case '-':
		return 20;
	case '_':
		return 30;
	default:
		return 0;
	}
}

void TIMER32_0_IRQHandler (void) {
	LPC_TMR32B0->IR = 1; //clear interrupt flag
}

//here play song
void TIMER32_1_IRQHandler (void) {
	LPC_TMR32B1->IR = 1; //clear interrupt flag
	static int32_t counter = 0;
	static const char song[] = "E8.D2-E2-F2-E2-D2-C4-C8_" "D4.E2-F4-F8_F2-F2-F2-E2-D4_D8_";
	static const char* note = song;
	static int8_t pause = FALSE;
	int16_t pitch;

	if (is_play_song == FALSE){
		return;
	}

	if (counter) {
		if (counter%2){
			P1_2_HIGH();
		} else {
			P1_2_LOW();
		}
		counter--;
	} else if (pause) {
		LPC_TMR32B1->MR0 = getPause(*note) * 1000;
		note++;
		pause = FALSE;
	} else if (counter == 0) {
		pitch = getNote(*note);
		if (pitch=='\0'){
			note = song;
			is_play_song = FALSE;
			LPC_TMR32B1->MR0 = 1000;
			return;
		}
		note++;
		int period2 = 500000/pitch;
		LPC_TMR32B1->MR0 = period2; // 1/(pitch[Hz]*2)
		counter = getDuration(*note) * 1000 / period2;
		note++;
		pause = TRUE;
	}
}

void int_init(int TimerInterval){
	LPC_SYSCON->SYSAHBCLKCTRL |= (1<<10);

	LPC_TMR32B1->TCR = 2; /* reset timera */
	LPC_TMR32B1->PR = SystemFrequency / LPC_SYSCON->SYSAHBCLKDIV / 1000000 - 1; /* Preskler na 1 mikrosekunde */
	LPC_TMR32B1->MR0 = TimerInterval;
	LPC_TMR32B1->IR = 0xFF; /* usun wszystkie oczekujace przerwania */

	LPC_TMR32B1->MCR = 3;                        /* Interrupt and Reset on MR0 */
	LPC_TMR32B1->TCR = 1; /* start timer */

	/* Enable the TIMER1 Interrupt */
	NVIC_EnableIRQ(TIMER_32_1_IRQn);
}

int main (void) {
	//these will hold zero position of board from accelerometer
	int8_t xoff = 0;
	int8_t yoff = 0;
	int8_t zoff = 0;

	//here goes data from accelerometer
	int8_t x = 0;
	int8_t y = 0;
	int8_t z = 0;

	int16_t snek_speed = 10;
	uint8_t state = 0;
	uint8_t prev_state = 0;
	uint32_t joystick_counter = 0;
	uint8_t acc_state = 0; //0bLeftRightDownUpCenter

	GPIOInit();
	init_timer32(0, 10);

	I2CInit( (uint32_t)I2CMASTER, 0 );
	SSPInit();
	ADCInit( ADC_CLK );

	rotary_init();
	led7seg_init();

	pca9532_init();
	joystick_init();
	acc_init();
	oled_init();
	rgb_init();

	//setting up light sensor
	light_init();
	light_enable();
	light_setRange(LIGHT_RANGE_4000);


	//assume base board in zero position when reading first value
	acc_read(&x, &y, &z);
	xoff = x;
	yoff = y;
	zoff = z-64;


	/* ---- Speaker ------> */


	GPIOSetDir( PORT1, 9, 1 );
	GPIOSetDir( PORT1, 10, 1 );

	GPIOSetDir( PORT3, 0, 1 );
	GPIOSetDir( PORT3, 1, 1 );
	GPIOSetDir( PORT3, 2, 1 );
	GPIOSetDir( PORT1, 2, 1 );

	GPIOSetValue( PORT3, 0, 0 );  //LM4811-clk
	GPIOSetValue( PORT3, 1, 0 );  //LM4811-up/dn
	GPIOSetValue( PORT3, 2, 0 );  //LM4811-shutdn

	/* <---- Speaker ------ */

	GPIOSetDir(PORT0, 1, 0);
	LPC_IOCON->PIO0_1 &= ~0x7;

	oled_clearScreen(OLED_COLOR_BLACK);

	//init_timer32(1, snek_speed);
#define NO_MUSIC
#ifndef NO_MUSIC
	int_init(/*snek_speed*/ 10);
#endif

	uint16_t letters_state = 0;
	const uint8_t letters_speed = 10; //change letter each 10 iterations
	const uint8_t letters[] = "NO STEP ON SNECC ";
	const uint8_t letters_size = sizeof(letters);
	while (TRUE) {
		//display letter on 7 segment display
		letters_state %= letters_size * letters_speed;
		led7seg_setChar(letters_state % letters_speed ? letters[letters_state / letters_speed] : ' ', FALSE);
		letters_state++;

		blinkLEDs(0);

		int16_t new_snek_speed = 4000 / ADCRead(0) + 20;
		if (new_snek_speed != snek_speed) {
			snek_speed = new_snek_speed;
		}

		//read accelerometer
		if(acc_status == GOOD) {
			acc_state=0;
			acc_read(&x, &y, &z);

			x -= xoff;
			y -= yoff;
			z -= zoff;

			uint16_t sum = x*x+y*y+z*z;

			if(sum < 2500 || sum > 6000) {
				acc_status = BROKEN;
				acc_state=0;
			} else {
				acc_state |= (y<-10)<<UP;
				acc_state |= (y>10)<<DOWN;
				acc_state |= (x<-10)<<LEFT;
				acc_state |= (x>10)<<RIGHT;
			}
		}

		if (joystick_status != BROKEN) {
			state = joystick_read();
			if (state == prev_state) {
				joystick_counter += 1;
			} else {
				joystick_counter = 0;
			}

			prev_state = state;

			if (joystick_counter == STOPJOYSTICK) {
				if ((state|acc_state) != 0) {
					joystick_status = BROKEN;
					state = 0;
					joystick_counter = 0;
				} else {
					joystick_status = PAUSE;
				}
			}
		}

		if (joystick_status != PAUSE) {
			processSnake(state|acc_state);
		} else if (state != 0) {
			joystick_status = GOOD;
		}

		delay32Ms(0, snek_speed);
	}

	return 0;
}

