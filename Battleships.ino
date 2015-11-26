#include <SoftwareSerial.h>
#include <stdbool.h>

//Tansmission Definitions
#define SOT 's' //Start of transmission
#define EOT 'e' //End of transmission
#define NL 'n' //New line

//Pin Definitions
#define RXPIN 12 
#define TXPIN 13
#define POTPIN A0
#define BUTTONPIN 2
#define BUZZPIN 0

//Game definitions
#define MAXSHIPS 3
#define SHIPTOTAL 8

//Game Variables
int ledPin[] = {4, 5, 6, 7, 8, 9, 10, 11};
int gameState = 0; //0 is menu, 1 is choosing ships, 2 is attacking ships, 3 is game over
volatile bool buttonPressed;
int activePlayer;
int myPlayer = -1;
int otherPlayer = -1;
bool playerReady[2];
bool playerWon[2];
int shipLocation[2][MAXSHIPS];
bool shipTaken[SHIPTOTAL];
bool errorMode = true;
bool warningMode = true; 
bool debugMode = true; 

//SoftwareSerial
SoftwareSerial serial(RXPIN, TXPIN);

//Prints out an error message to the debug
void err(char* message) {
	if (errorMode) {
		Serial.print("ERROR: ");
		Serial.println(message);
	}
}

void warn(char* message) {
	if (warningMode) {
		Serial.print("WARNING: ");
		Serial.println(message);
	}
}

void deb(char* message) {
	if (debugMode) {
		Serial.print("DEBUG: ");
		Serial.println(message);
	}
}

void buttonInterrupt() {
	buttonPressed = true;
}

//Starts here
void setup() {
	comSetup();
	gameSetup();
}

//Main Loop
void loop() {
	clearLEDs();
	comReceive();
	gameLoop();
	//musicLoop();
}

//Setup Communication
void comSetup() {
	//Setup SoftwareSerial
	pinMode(RXPIN, INPUT);
	pinMode(TXPIN, OUTPUT);
	serial.begin(57600);

	//Setup Debug Serial
	if (debugMode || errorMode || warningMode) Serial.begin(57600);

	//Wait for connection
	deb("Waiting for connection");
	bool step1 = false;
	bool step2 = false;
	char data;

	flushSerial(); //In case other device has been on for a while

	while (!step2) {
		delay(500);

		if (serial.available() > 0) {
			data = serial.read();
			switch (data) {
				case 'h':
					step1 = true;
					deb("Received h");
				break;
				case 'i':
					step1 = true;
					step2 = true;
					deb("Received i");
					serial.print('i');
				break;
				default:
					err("Out of sync. We are making connection, other player is elsewhere");
				break;
			}
		}

		if (!step1) {
			serial.print('h');
			deb("Pushing h");
		} else {
			serial.print('i');
			deb("Pushing i");
		}
	}

	deb("Connection made");
	flushSerial();
	delay(500);

	//Decide player numbers
	deb("Deciding player numbers");
	while(otherPlayer == -1) {
		delay(500);
		if (serial.available() > 0) {
			data = serial.read();
			switch (data) {
				case '0': {
					otherPlayer = 0;
					myPlayer = 1;
					deb("I am player 1");
					serial.print('1');
					break;
				}

				case '1': {
					otherPlayer = 1;
					myPlayer = 0;
					deb("I am player 0");
					serial.print('0');
					break;
				}

				default:
					err("Out of sync. We are deciding pNum, other player is elsewhere");
				break;
			}
		} else {
			serial.print('0');
		}
	}

	deb("Player numbers have been assigned.");
	flushSerial();
	delay(500);
}

//Receive communication
void comReceive() {
	if (serial.available() > 0) { //If there is anything in the serial receive buffer
		if (serial.read() == SOT) { //If the first character in the serial buffer indicates the start of a transmission
			//Temporary variables for the other player's number and game mode
			int pNum = serial.read();
			int gState = serial.read();
			
			if (pNum != otherPlayer) {
				//Player number discrepency, attempt to reassign player numbers
				err("pNum Sync");
			}
			
			if (gState != gameState) {
				//Game states are out of sync
				err("gState Sync");
			}

			//The rest of the packet structure depends on the current gameState
			switch (gameState) {
				case 0: { //Menu
					deb("OtherPlayerReady:");
					playerReady[pNum] = serial.read();
					break;
				}

				case 1: { //Selecting player's own ships
					for (int i=0; i<MAXSHIPS; i++) {
						shipLocation[pNum][i] = serial.read();
					}
					playerReady[pNum] = serial.read();
					break;
				}

				case 2: { //Attacking the other player's ships
					playerReady[pNum] = serial.read();
					playerWon[pNum] = serial.read();
					break;
				}

				case 3: { //Game over
					playerReady[pNum] = serial.read();
					break;
				}
			}

			if (serial.read() != EOT) { //If the next character is not the end of transmission character, give a warning
				warn("Unexpected packet structure");
			}
		} else {
			warn("Received packet that doesn't start with SOT, flushing serial buffer");
			flushSerial();
		}	
	}
}

//Game Setup
void gameSetup() {
	deb("Starting gameSetup");
	attachInterrupt(digitalPinToInterrupt(BUTTONPIN), buttonInterrupt, FALLING);
	for (int i=0; i<countLEDs(); i++) {
		pinMode(ledPin[i], OUTPUT);
	}
}

//Main Game Loop
void gameLoop() {
	int ship = readPotentiometer();
	switch (gameState) {
		case 0: { //Menu
			//Play music until both players push their button
			//Do some fancy shit with LEDs
			//Check for button presses
			if (buttonPressed && !playerReady[myPlayer]) {
				playerReady[myPlayer] = true;
				serial.print(SOT);
				serial.write(myPlayer);
				serial.write(gameState);
				serial.write(playerReady[myPlayer]);
				serial.print(EOT);
				buttonPressed = false;
			}

			if (playerReady[0] && playerReady[1]) {
				//Move on to next game state
				gameState = 1;
				deb("Moving to gameState 1");
				playerReady[0] = false;
				playerReady[1] = false;
			}
			break;
		}
		
		case 1: { //Selecting player's own ships
			//Take potentiometer input
			digitalWrite(ledPin[ship], HIGH);

			//Take button input to choose my ship placement
			if (buttonPressed && !shipTaken[ship]) {
				shipTaken[ship] = true;
				shipLocation[myPlayer][nextFreeShip()] = ship;
				buttonPressed = false;
			}

			//If there are no more ships to choose
			if (nextFreeShip() == -1 && !playerReady[myPlayer]) {
				playerReady[myPlayer] = true;
				serial.print(SOT);
				serial.print(myPlayer);
				serial.print(gameState);
				for (int i=0; i<MAXSHIPS; i++) { //Send my ship positions
					serial.print(shipLocation[myPlayer][i]);
				}
				serial.print(playerReady[myPlayer]);
				serial.print(EOT);
			}

			if (playerReady[0] && playerReady[1]) {
				//Move to next game state
				gameState = 2;
				deb("Moving to gameState 2");
				playerReady[0] = false;
				playerReady[1] = false;
				//Set first players turn
				activePlayer = 0;
			}
			break;
		}

		case 2: { //Attacking the other player's ships
			//Check that it's my turn
			if (activePlayer == myPlayer) {
				//Take potentiometer input
				digitalWrite(ledPin[ship], HIGH);

				//Take button input to attack
				if (buttonPressed) {
				}
			}
			break;
		}

		case 3: { //Game over
			//
			break;
		}
	}
}

void musicLoop() {
	switch (gameState) {
		case 0: { //Menu music

			break;
		}

		case 1: { //Selecting ships music

			break;
		}

		case 2: { //Attacking music

			break;
		}

		case 3: { //Game over music

			break;
		}
	}
}

int readPotentiometer() {
  int valueRead = analogRead(POTPIN);
  if(valueRead <= 128){
    return 1; }
  else if(valueRead<=256){
    return 0; }
  else if(valueRead<=384){
    return 7; }
  else if(valueRead<=512){
    return 6; }
  else if(valueRead<=640){
    return 5; }
  else if(valueRead<=768){
    return 4; }
  else if(valueRead<=896){
    return 3; }
  else {
    return 2; }
}

void flushSerial() {
	deb("Flushing serial");
	char data;
	while(serial.available() > 0) {
		data = serial.read();
	}
}

void clearLEDs() {
	for (int i=0; i<countLEDs(); i++) {
		digitalWrite(ledPin[i], LOW);
	}
}

int countLEDs() {
	return(sizeof(ledPin) / sizeof(ledPin[0]));
}

int nextFreeShip() {
	for (int i=0; i<MAXSHIPS; i++) {
		if (shipLocation[myPlayer][i] == -1) {
			return(i);
		}
	}
	return(-1);
}