#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "alt_types.h"
#include "sys/alt_irq.h"
#include "system.h"     //includes #defines for the custom system like BUTTONS_BASE etc.
#include "altera_avalon_uart_regs.h" //Altera's header file of functions for operating on the UART registers
#include "altera_avalon_pio_regs.h" //Altera's header file of functions for operating on the PIO registers
#include "sys/alt_alarm.h"


typedef enum{
UP = 0, DOWN, IDLE
} Direction;

int currentFloor = 1;
Direction motorDirection = IDLE;
int requestedFloor = 1;
Direction requestedDirection = UP;
int elevatorButtons[] = {0,0,0,0};
int callButtons[] = {0,0,0,0,0,0,0,0};
int callButtonPriority = 1;
int doorOpen;
FILE* fp;
alt_alarm timer; // timer

void handle_button_interrupts(void* context, alt_u32 id);
void init_button_interrupts();
void motor_control();
void callButtons_add(int floor, int down);
void callButtons_remove(int floor, int down);
int callButtons_priority(int floor, int down);
int set_destination();
int store_new_requests();
void print_elevator_status();


/************************************************************************
 * timeout function
 ************************************************************************/
alt_u32 timeout_function(void* context)
{
	// Write your own code here to :
	// 1. Implement functionality to close the door when timeout  happens
	// 2. Return an appropriate value for this function
	 char* formattedString = "Closing Door\n";
	 fprintf(fp,"%s",formattedString);

	 doorOpen = 0;

	 return 0;
}


/************************************************************************
 * handle_button_interrupts
 * Description: Interrupt handler for push button 1, increments current
 * floor if motor direction is up or decrements current floor if motor
 * direction is down.
 * Inputs:      None
 * Outputs:     None
 ************************************************************************/
void handle_button_interrupts(void* context, alt_u32 id){
  //Reset the edge capture register on button PIO 1
  IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTONS_BASE, 0);

  //TEST HERE
  /*
  char* formattedString = "Motor is working:\n\n";


     fp = fopen("/dev/uart", "r+");

 	if (fp != NULL) {
 		fprintf(fp,"%s",formattedString );
 	}


 //TEST HERE*/

  switch (motorDirection){
  case UP:
    currentFloor++;
    motor_control();
    break;
  case DOWN:
    currentFloor--;
    motor_control();
    break;
  case IDLE:
    break;
  default:
    break;
  }
}


/************************************************************************
 * init_button_interrupts
 * Description: Enables interrupts for push button 1 and assigns
 * handle_button_interrupts() as the interrupt handler.
 * Inputs:      None
 * Outputs:     None
 ************************************************************************/
void init_button_interrupts(){
  /* write your own code here to initialize an interrupt that will
   * be generated when push button 1 is pressed, the interrupt handler above
   * handle_button_interrupts(void* context, alt_u32 id) is already provided
   * to handle this interrupt.
   */

  //Clearing the edge capture register
  IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTONS_BASE, 0);
  //Enable interrups for button one
  IOWR_ALTERA_AVALON_PIO_IRQ_MASK(BUTTONS_BASE, 0x2);
  //Register the ISR
  alt_irq_register(BUTTONS_IRQ, NULL, handle_button_interrupts);

}



/************************************************************************
 * callButtons_add
 * Description: Stores a priority for the specified call up or down button request
 *              in the 'callButtons' array. The priority of the specified call
 *              request depends on the order of the call button requests that are
 *              waiting to be serviced. A priority of 0 means that the button has
 *              not been pressed, and a priority of 1 is the highest priority.
 * Inputs:      floor: floor of the call up/down button pressed
 *              down: 1 if the call down button pressed or 0 for call up button
 * Outputs:     None (but, modifies global array)
 ************************************************************************/
void callButtons_add(int floor, int down){
  int index = 2 * (floor - 1) + down;

  if(index < 8 && index >= 0){
    callButtons[index] = callButtonPriority;
    callButtonPriority++;
  }
}


/************************************************************************
 * callButtons_priority
 * Description: Returns the priority of the specified call up or call down button
 *              from the 'callButtons' array.
 * Inputs:      floor: floor of the call up/down button serviced
 *              down: 1 if the call down button serviced or 0 for call up button
 * Outputs:     The priority of the call button request
 ************************************************************************/
int callButtons_priority(int floor, int down){
	int index = 2 * (floor - 1) + down;
	return callButtons[index];

}


/************************************************************************
 * callButtons_remove
 * Description: Clears (sets to zero) the specified call up or down request from the 'callButtons'
 *              array to indicate that it has been serviced and adjusts the
 *              priority of all other waiting requests as required (we may
 *              service a request that is not the highest priority).
 * Inputs:      floor: floor of the call up/down button serviced
 *              down: 1 if a call down button was serviced or 0 for call up button
 * Outputs:     None (modifies global array)
 ************************************************************************/
void callButtons_remove(int floor, int down){
  /* write your own code here that clears the priority of the specified
   * call button once it is serviced and adjusts the priority of all
   * other waiting requests.
   */

    //gets index of floor in callButtons
	int index = 2 * (floor - 1) + down;


	//Decrement callButtonPriority
	callButtonPriority--;

	//Loops through the callButtons array and decrements all values by 1
	int i;
	for (i = 0; i < 8; i++) {
		if ((callButtons[i] != 0) && (callButtons[i] > callButtons[index]) && (callButtons[i] != callButtons[index])) {
			callButtons[i] = callButtons[i] - 1;
		}
	}

	//set specific cell to 0
	callButtons[index] = 0;
}


/************************************************************************
 * set_destination
 * Description: Sets the destination of the elevator and the direction it will
 *              carry passengers based on the current floor, current
 *              direction, call up/down requests (and priorities) and
 *              elevator buttons.
 * Inputs:      None
 * Outputs:     1 if a destination for the elevator was found otherwise 0
 ************************************************************************/
int set_destination(){
  /* write your own code here to set the value of the global variables
    * 'requestedFloor' and 'requestedDirection' based on the following
    * pseudo-code.
    *
    * If the elevator is moving down to service a call down or elevator button request:
    * (1) If there is a call down request on the floor below then the elevator
    *     should stop and service this.
    * (2) Otherwise the elevator should service the closest elevator button below.

    * If the elevator is moving up to service a call up or elevator button request:
    * (1) If there is a call up request on the floor above then the elevator
    *     should stop and service this.
    * (2) Otherwise the elevator should service the closest elevator button above.
    *
    * If the elevator is moving down to service a call up request or moving up to
    * service a call down request or is idle (all other possibile elevator states):
    * (1) The elevator should service the highest priority call request.
    * (2) Otherwise when there are no pending call requests the elevator should
    *     serivce any elevator buttons in the wrong direction.
    * (3) If there are also no pending elevator buttons then return 0 to put the
    *     lift into an IDLE state.
    */


	int i, j, k, l, m, n;
	int noCallButtons;

	//------Down info---------//
	//If there is a service call down, find the nearest floor
	int serviceDown = 0;
	int serviceDownFlag = 0;
	for (i = 1; i < (currentFloor*2) - 1; i = i + 2) {
		if (callButtons[i] != 0) {
			serviceDown = i;
			serviceDownFlag = 1;
		}
	}
	//If there is a elevator button request down from current floor
	int elevatorDown = 0;
	int elevatorDownFlag = 0;
	for (j = 0; j <= currentFloor - 2; j++) {
		if (elevatorButtons[j] != 0) {
			elevatorDown = j + 1; //store the closest down request inside the elevator
			elevatorDownFlag = 1;
		}
	}


	//------Up info---------//
	//If there is a service call up, find the nearest floor
	int serviceUp = 0;
	int serviceUpFlag = 0;
	for (k = 6; k > (currentFloor*2) - 2; k = k - 2) {
		if (callButtons[k] != 0) {
			serviceUp = k+1;
			serviceUpFlag = 1;
		}
	}
	//If there is a elevator button request up from current floor
	int elevatorUp = 0;
	int elevatorUpFlag = 0;
	for (l = 3; l > currentFloor - 1; l--) {
		if (elevatorButtons[l] != 0) {
			elevatorUp = l+1; //store the closest up request inside the elevator
			elevatorUpFlag = 1;
		}
	}


	//------Move down---------//
	if ((motorDirection == DOWN) && ((serviceDownFlag != 0) || (elevatorDownFlag != 0))) {
		//If there is a call down request on floor below
		if (callButtons[(currentFloor*2) - 3] != 0) {
			requestedFloor = currentFloor - 1;
			requestedDirection = DOWN;;
			return 1;
		}
		//Find the closest floor down
		if (((serviceDown+1)/2) > elevatorDown && (elevatorDown != 0)){
			requestedFloor = (serviceDown+1)/2;
		} else {
			requestedFloor = elevatorDown;
		}
		requestedDirection = DOWN;
		return 1;
	}
	//-----Move up---------//
	else if ((motorDirection == UP) && ((serviceUpFlag != 0) || (elevatorUpFlag != 0))){
		//If there is a call up request on floor above
		if (callButtons[currentFloor * 2] != 0) {
			requestedFloor = currentFloor + 1;
			requestedDirection = UP;
			return 1;
		}
		//Find the closest floor up
		if (((serviceUp+2)/2) > elevatorUp && (elevatorUp != 0)){
			requestedFloor = elevatorUp;
		} else {
			requestedFloor = (serviceUp+2)/2;
		}
		requestedDirection = UP;
		return 1;
	}
	//-----Other requests---------//
	else {
		//Finding the index of the highest priority call
		int priorityIndex = 0;
		int priorityFlag = 0;
		for (m = 0; m < 8; m++) {
			if (callButtons[m] == 1) {
				priorityIndex = m;
				priorityFlag = 1;
				break;
			}
		}

		//Checking if there are any floor service requests
		for (n = 0; n <= 3; n++) {
			if (elevatorButtons[n] != 0) {
				noCallButtons = 0;
				break;
			} else {
				noCallButtons = 1;
			}
		}

		if (priorityFlag != 0) {
			if ((priorityIndex % 2) == 1) { //Then highest priority call is a down call
				requestedFloor = ((priorityIndex - 1) / 2) + 1;
				requestedDirection = DOWN;
				return 1;
			} else {
				requestedFloor = (priorityIndex / 2) + 1;
				requestedDirection = UP;
				return 1;
			}
		} else if (noCallButtons == 0) {
			requestedFloor = n + 1;
			if (currentFloor < requestedFloor) {
				requestedDirection = UP;
			} else {
				requestedDirection = DOWN;
			}
			return 1;
		} else {
			return 0;
		}
	}
	return 0;
}


/************************************************************************
 * motor_control
 * Description: Controls the direction of the elevator motor in response
 * to changes in the current floor and requested floor.
 * Inputs:      None
 * Outputs:     None
 ************************************************************************/
void motor_control(){
  char formattedString[80];

  if (requestedFloor == currentFloor){
    motorDirection = IDLE;
    sprintf(formattedString,"Motion: stopped on floor %d (req: %d)\n",currentFloor, requestedFloor);

  }
  else if(requestedFloor > currentFloor){
    motorDirection = UP;
    sprintf(formattedString,"Motion: moving up on floor %d\n",currentFloor);

  }
  else{
    motorDirection = DOWN;
    sprintf(formattedString,"Motion: moving down on floor %d\n",currentFloor);

  }

    fprintf(fp,"%s",formattedString);

}


/************************************************************************
 * store_new_requests
 * Description: Reads characters in from the rxBuffer and interprets these
 * as call up/down or floor button requests for elevator with 4 floors.
 * ie. b1 -> b4 are floor buttons 1-4, u1-u3 are call up buttons and d2-d4
 * are call down buttons. Once requests are interpreted the destination
 * floor and direction are evaluated and stored in the elevator button
 * request array or call up/down priority queue.
 * Inputs:      None
 * Outputs:     1 if a new request was added otherwise 0
 ************************************************************************/
int store_new_requests(){
  char uartCharacter;
  Direction buttonDirection;
  int buttonFloor;

    while((uartCharacter = getc(fp)) != '\0'){
    //parse string and resolve variable requestedFloor
    switch (uartCharacter){
      case 'u':
        buttonDirection = UP;
        break;
      case 'd':
        buttonDirection = DOWN;
        break;
      case 'b':
        buttonDirection = IDLE;
        break;
      default:
        continue;
        break;
    }

    //wait for floor of request to follow

     while((uartCharacter = getc(fp)) == '\0'){}

    switch (uartCharacter){
      case '1':
        if(buttonDirection != DOWN){
          buttonFloor = 1;
        }
        else{
          continue;
        }
        break;
      case '2':
        buttonFloor = 2;
        break;
      case '3':
        buttonFloor = 3;
        break;
      case '4':
        if(buttonDirection != UP){
          buttonFloor = 4;
        }
        else{
          continue;
        }
        break;
      default:
        continue;
        break;
    }

    //store request in array
    if (buttonDirection == IDLE){
      elevatorButtons[buttonFloor-1] = 1;
      printf("button on %d\n", buttonFloor);
    }
    else{
    	printf("call on %d\n", buttonFloor);
		callButtons_add(buttonFloor, buttonDirection);
    }
    return(1);
  }
  return(0);
}


/************************************************************************
 * print_elevator_status
 * Description: Prints the outstanding call up/down service requests,
 * elevator button service requests, current floor and elevator desitnation
 * to the console via the uart connection.
 * Inputs:      None
 * Outputs:     None
 ************************************************************************/
void print_elevator_status(){
  char formattedString[80];
  sprintf(formattedString, "Button Status: u1=%d, u2=%d, u3=%d, d2=%d, d3=%d, d4=%d, b1=%d, b2=%d, b3=%d, b4=%d\n",callButtons[0],callButtons[2],callButtons[4],callButtons[3],callButtons[5],callButtons[7],elevatorButtons[0],elevatorButtons[1],elevatorButtons[2],elevatorButtons[3]);

    fprintf(fp,"%s",formattedString);


  if (requestedDirection == UP){
    sprintf(formattedString, "Servicing Request: floor %d, up\n",requestedFloor);

    fprintf(fp,"%s",formattedString);

  }
  else{
    sprintf(formattedString, "Servicing Request: floor %d, down\n",requestedFloor);

    fprintf(fp,"%s",formattedString);
  }
}


/************************************************************************
 * main
 * Description: Elevator controller for four story building.
 * Inputs:      None
 * Outputs:     None
 ************************************************************************/
int main(void){


	FILE *lcd;
	lcd = fopen(LCD_NAME, "w");
	fprintf(lcd, "Welcome to the lift controller\n\n");
	printf("Lift controller is live\n");
	fclose(lcd);

  int newRequest = 0, destinationValid = 0;
  char* formattedString = "NIOS II ELEVATOR CONTROLLER\n";


    fp = fopen("/dev/uart", "r+");

	if (fp != NULL) {
		fprintf(fp,"%s",formattedString );
	}

  motor_control();
  init_button_interrupts();

  while(1){

	/*
	  * store_new_requests uses getc to read UART input.
	  * This is a blocking read. Hence, this will stall the
	  * program. To prevent this, implement a condition
	  * over the function call ("newRequest = store_new_requests();".
	  * The condition being when switch 17 (SW17)
	  * is switch on to be true.
	*/

	if (IORD_ALTERA_AVALON_PIO_DATA(SWITCHES_BASE) == 131072) {
		printf("Switch 17 is live\n");
		newRequest = store_new_requests();
	}

    if(newRequest){
		destinationValid = set_destination();
		motor_control();
		print_elevator_status();
		newRequest = 0;
    }

    //printf("Current floor is: %d. Requested floor is: %d \n", currentFloor, requestedFloor);

    if(requestedFloor == currentFloor && destinationValid && !doorOpen){
		callButtons_remove(requestedFloor, requestedDirection);
		elevatorButtons[requestedFloor-1] = 0;
		formattedString = "Opening Door\n";

		fprintf(fp,"%s",formattedString);

		//usleep(1000000);
		//formattedString = "Closing Door\n";
		//fprintf(fp,"%s",formattedString);

		doorOpen = 1;

		alt_alarm_start(&timer, 5000, timeout_function, 0);

		//store_new_requests();
		destinationValid = set_destination();
		motor_control();
		print_elevator_status();
    }

  }

  fclose(fp);

  return 0;
}

