//
//			ARDUINO BATTLESHIPS
//
//			AUTHORS:			
//			  MATT BUCKLEY
//			  RICCARDO BROGGI
//


/*
Packet structure:

	Start of Transmission (SOT)
	Player Number (To check we're in sync with other device)
	Game State (To check we're in sync with other device)

	--The rest of the data is dependant on the gameState

	End of Transmission (EOT)

*/





// ------------------------ Definitions -----------------------

#include <SoftwareSerial.h>
#include <stdbool.h>

//Tansmission Definitions
#define SOT 's' //Start of transmission char
#define EOT 'e' //End of transmission char

//Pin Definitions
#define RXPIN 3
#define TXPIN 13
#define POTPIN A0
#define BUTTONPIN 2
#define SPEAKERPIN 12

//Game definitions
#define MAXSHIPS 3 //Maximum number of ships a player can have
#define SHIPTOTAL 8 //Number of LEDs

//Game Variables
int ledPin[] = {4, 5, 6, 7, 8, 9, 10, 11};
int speedInitial[] = {150, 100, 50, 30, 20}; //For light display
int gameState = 0; //0 is menu, 1 is choosing ships, 2 is attacking ships, 3 is game over
volatile bool buttonPressed;
int activePlayer; //Which player's turn is it?
int myPlayer = -1; //Default is -1 for undecided
int otherPlayer = -1; //Default is -1 for undecided
bool playerReady[2]; //Ready to continue to next game state
bool playerWon[2]; //If a player wins, they send playerWon[Their Player Number] = true
int shipLocation[2][MAXSHIPS]; //LED locations of each player's ships
bool shipTaken[SHIPTOTAL]; //Stores if an LED already has a ship assigned to it
bool shipDestroyed[2][MAXSHIPS]; //Keeps track of whether each player's ships are destroyed
bool errorMode = true;
bool warningMode = true; 
bool debugMode = true;

//SoftwareSerial
SoftwareSerial serial(RXPIN, TXPIN);

//Sounds
int winTones[] = {523, 587, 659, 698, 784, 880, 1046, 1174};

// ------------------------ Setup and Loop -----------------------

void setup() {
	comSetup();
	gameSetup();
}

//Main Loop
 void loop() {
	clearLEDs();
	comReceive(); //Receive Communication
	gameLoop(); //Main game loop, contains game logic and sends data
}


// ------------------------ Function definitions -----------------------

// Define the ISR to call when the interruptFunction is called.
void buttonInterrupt() {
	buttonPressed = true;
}


//Setup Communication. Connections are made and player numbers decided here
void comSetup() {
	//Setup SoftwareSerial
	pinMode(RXPIN, INPUT);
	pinMode(TXPIN, OUTPUT);
	serial.begin(57600);

	//Setup Debug Serial
	if (debugMode || errorMode || warningMode) Serial.begin(57600);

	//Flush serial buffer in case other device has been on for a while, our buffer may be full if our reset button has been pushed
	flushSerial();


	//Wait for connection
	//This involved 3 steps
	//Step 1: send 'h'
	//Step 2: has received h, so send 'i'
	//Step 3: if you receive an 'i', a connection has been made. Send 'i' to tell the other device
	deb("Waiting for connection");
	bool step1 = false;
	bool step2 = false;
	char data;

	while (!step2) {
		if (serial.available() > 0) { //If we have received anything
			data = serial.read(); //Read received data
			switch (data) {
				case 'h': //If we received h
					step1 = true;
				break;
				case 'i': //If we received i
					step1 = true;
					step2 = true;
					serial.print('i'); //Send i
				break;
			}
		}
		if (!step1) { //If we haven't received h yet
			serial.print('h'); //Send h
		} else {
			serial.print('i'); //Else, send i
		}
	}

	//Connection made. Flush serial buffer in case any 'h' or 'i' are left in the buffer
	deb("Connection made");
	flushSerial();


	//Decide player numbers
	deb("Deciding player numbers");
	while(otherPlayer == -1) { //While we don't know the other player's number
		if (serial.available() > 0) { //If we received data
			data = serial.read(); //Read data. This data is the other player's number
			switch (data) {
				case '0': { //Received other player is 0. We are 1
					otherPlayer = 0;
					myPlayer = 1;
					deb("I am player 1");
					serial.print('1');
					break;
				}
				case '1': { //Received other player is 1. They accepted us as player 0
					otherPlayer = 1;
					myPlayer = 0;
					deb("I am player 0");
					serial.print('0');
					break;
				}
			}
		} else {
			//If we haven't received anything, tell other player we are 0 and see if we get a 1 back from them
			serial.print('0'); //Send 0
		}
	}

	//Numbers assigned. Flush serial buffer in case any '0' or '1' are left in the buffer
	deb("Player numbers have been assigned.");
	flushSerial();
}


//Receive communication
void comReceive() {
	if (serial.available() > 0) { //If there is anything in the serial receive buffer
		if (serial.read() == SOT) { //If the first character in the serial buffer indicates the start of a transmission
			deb("Received SOT");
			//Temporary variables for the other player's number and game mode
			int pNum = serial.read();
			int gState = serial.read();
			deb("Received pNum", pNum);
			deb("Received gState", gState);

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
					//Receive whether or not they are ready to move to next gameState (if they pushed their button)
					playerReady[pNum] = serial.read();
					deb("Received pReady", playerReady[pNum]);
					if (playerReady[otherPlayer]) deb("Other player is ready.");
					break;
				}

				case 1: { //Selecting player's own ships
					//Recived other player's ship locations
					for (int i=0; i<MAXSHIPS; i++) {
						shipLocation[pNum][i] = serial.read();
						deb("Received Ship Location", shipLocation[pNum][i]);
					}
					//Receive whether or not they are ready to move to next gameState (If they selected MAXSHIPS ships)
					playerReady[pNum] = serial.read();
					deb("Received pReady", playerReady[pNum]);
					if (playerReady[otherPlayer]) deb("Other player has selected ships and is ready.");
					break;
				}

				case 2: { //Attacking the other player's ships
					//Received the destroyed ship
					//225 means that they missed their shot
					int shipTheyDestroyed = serial.read(); //Read data
					if (shipTheyDestroyed != 255) { //If they actually hit a ship
						shipDestroyed[myPlayer][shipTheyDestroyed] = true;
						//Play explosion sound
						exploded(shipLocation[myPlayer][shipTheyDestroyed]);
						deb("They destroyed our ship", shipTheyDestroyed);
					} else {
						deb("They missed their shot", shipTheyDestroyed);
					}
					//Receive which player's turn it is
					activePlayer = serial.read();
					deb("Received active player: ", activePlayer);
					//Receive whether or not the other player has won
					playerWon[otherPlayer] = serial.read();
					deb("Received other player won: ", playerWon[otherPlayer]);
					break;
				}
			}

			if (serial.read() != EOT) { //If the next character is not the end of transmission character, give a warning
				warn("Unexpected packet structure");
			} else {
				deb("Received EOT");
			}
		} else {
			//There were incomplete packets left in the buffer from a previous gameState (usually setup)
			warn("Received packet that doesn't start with SOT, flushing serial buffer");
			flushSerial();
		}	
	}
}

//Game Setup
void gameSetup() {
	deb("Setting up game");
	//Set up button
	attachInterrupt(digitalPinToInterrupt(BUTTONPIN), buttonInterrupt, FALLING);
	//Set LED pinModes
	for (int i=0; i<countLEDs(); i++) {
		pinMode(ledPin[i], OUTPUT);
	}
	//Set all ship locations to default value -1 to indicate an unset location
	for (int i=0; i<MAXSHIPS; i++) {
		shipLocation[myPlayer][i] = -1;
	}
	//Set speaker pinMode
	pinMode(SPEAKERPIN, OUTPUT);
}


//Main Game Loop, all communication is sent from here
void gameLoop() {
	int ship = readPotentiometer(); //Get led LED that the potentiometer is currently pointing towards
	switch (gameState) {
		case 0: { //Menu
			//Check for button presses
			if (buttonPressed) {
				if (!playerReady[myPlayer]) {
					deb("My player is ready.");
					playerReady[myPlayer] = true;
					//Tell the other player we're ready to continue
					serial.print(SOT); //Send start of transmission char
					serial.write(myPlayer); //Send our player number to check we're in sync
					serial.write(gameState); //Send our gameState to check we're in sync
					serial.write(playerReady[myPlayer]); //Send whether or not we are ready (we are)
					serial.print(EOT); //Send end of transmission character
				}
				buttonPressed = false;
			}

			if (playerReady[0] && playerReady[1]) {
				//Move on to next game state
				gameState = 1;
				deb("Both players are ready. Moving to gameState 1");
				//Reset pReadys and flush serial buffer for anything left over from incomplete packets
				playerReady[0] = false;
				playerReady[1] = false;
				flushSerial();
			}
			break;
		}
		
		case 1: { //Selecting player's own ships
			//Take potentiometer input
			digitalWrite(ledPin[ship], HIGH);

			//Take button input to choose my ship placement
			if (buttonPressed) {
				if (!shipTaken[ship]) {
					shipTaken[ship] = true;
					shipLocation[myPlayer][nextFreeShip()] = ship;
				}
				buttonPressed = false;
			}

			//If there are no more ships to choose
			if (nextFreeShip() == -1 && !playerReady[myPlayer]) {
				deb("My player has selected ships and is ready.");
				playerReady[myPlayer] = true;
				serial.print(SOT);
				serial.write(myPlayer);
				serial.write(gameState);
				for (int i=0; i<MAXSHIPS; i++) { //Send my ship positions
					serial.write(shipLocation[myPlayer][i]);
				}
				serial.write(playerReady[myPlayer]);
				serial.print(EOT);
			}

			if (playerReady[0] && playerReady[1]) {
				//Move to next game state
				gameState = 2;
				flushSerial();
				deb("Both players are ready. Moving to gameState 2");
				playerReady[0] = false;
				playerReady[1] = false;
				//Set first players turn
				activePlayer = 0;
			}

			if (playerReady[myPlayer]) {
				//Light up the LEDs that represent our ships
				displayMyShips();
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
					missleFired();
					if (enemyShipAtLocation(ship) > -1 && enemyShipAtLocation(ship) <= MAXSHIPS) {
						//We have hit the enemy!
						deb("We have hit enemy ship", enemyShipAtLocation(ship));
						shipDestroyed[otherPlayer][enemyShipAtLocation(ship)] = true;
						//It's now their turn
						activePlayer = otherPlayer;
						//Check if we have won
						if (haveWeWon()) {
							//We won!
							playerWon[myPlayer] = true;
						}
						//Let's tell them
						serial.print(SOT);
						serial.write(myPlayer);
						serial.write(gameState);
						serial.write(enemyShipAtLocation(ship)); //Ship we just destroyed
						serial.write(activePlayer); //Player who's turn it is now (otherPlayer)
						serial.write(playerWon[myPlayer]); //Tell them if we've won
						serial.print(EOT);
						shipLocation[otherPlayer][enemyShipAtLocation(ship)] = -1;
					} else {
						//We missed
						deb("We missed!");
						//It's now their turn
						activePlayer = otherPlayer;
						//Let's tell them
						serial.print(SOT);
						serial.write(myPlayer);
						serial.write(gameState);
						serial.write(255); //Ship we just destroyed (We didn't)
						serial.write(activePlayer); //Player who's turn it is now (otherPlayer)
						serial.write(playerWon[myPlayer]); //Tell them if we've won
						serial.print(EOT);
					}
					buttonPressed = false;
				}
				if (playerWon[myPlayer]) {
					deb("We have won!");

					succeedSound();

					deb("Moving to gameState 3");
					gameState = 3;
				}
				if (playerWon[otherPlayer]) {
					deb("Other player has won");
					deb("Moving to gameState 3");
					gameState = 3;
				}
			} else {
				//It's their turn
				displayMyShips();
			}
			break;
		}

		case 3: { //Game over
			if (playerWon[myPlayer]) {
				lightDisplay();
			}
			break;
		}
	}
}

// Define a function that takes input from the potentiometer and splits the range of values
// the potentiometer can take into 7 "regions"; the potentiometer can take values from 0 to
// 1023 so every 128 corresponds to a a value, meaning it will light up the corresponding LED
// when the value returned by this function is used.
int readPotentiometer() {
	int valueRead = analogRead(POTPIN);
	if(valueRead <= 128){
		return 1;
	}
	else if(valueRead<=256){
		return 0;
	}
	else if(valueRead<=384){
		return 7;
	}
	else if(valueRead<=512){
		return 6;
	}
	else if(valueRead<=640){
		return 5;
	}
	else if(valueRead<=768){
		return 4;
	}
	else if(valueRead<=896){
		return 3;
	}
	else {
		return 2;
	}
}

// Define a function to flush the serial by looping through the available serial
// buffer and reading everything without doing anything with it so that by the end of the
// function the serial buffer is empty
void flushSerial() {
	deb("Flushing serial");
	char data;
	while(serial.available() > 0) {
		data = serial.read();
	}
}

// Define a function that turns off every LED;
void clearLEDs() {
	for (int i=0; i<countLEDs(); i++) {
		digitalWrite(ledPin[i], LOW);
	}
}

// Define a function that returns the number of LED pins in the LedPin array
int countLEDs() {
	return(sizeof(ledPin) / sizeof(ledPin[0]));
}

// Define a function that returns the next shipLocation which is set to -1. Because -1 is the default value, this is used to set the next ship when selecting ships at the start in gameState=1
// If all ships already have positions allocated, it returns -1
int nextFreeShip() {
	for (int i=0; i<MAXSHIPS; i++) {
		if (shipLocation[myPlayer][i] == -1) {
			return(i);
		}
	}
	return(-1);
}


//Returns they ship number (1, 2 or 3) at the location specified
//Returns -1 if there is no ship at the location
int enemyShipAtLocation(int location) {
	for (int i=0; i<MAXSHIPS; i++) {
		if (shipLocation[otherPlayer][i] == location) {
			return(i);
		}
	}
	warn("We returned -1 from enemyShipAtLocation");
	return(-1);
}

// Define a funciton that returns a boolean corresponding to whether the active player has won
// or not by looping through all of the ships, checking how many have been destroyed and checking 
// whether the number of ships destroyed corresponds to the number of total ships. If so, return true
// else return false
bool haveWeWon() {
	int numberShipsDestroyed = 0;
	for (int i=0; i<MAXSHIPS; i++) {
		if (shipDestroyed[otherPlayer][i]) {
			numberShipsDestroyed++;
		}
	}
	if (numberShipsDestroyed == MAXSHIPS) {
		return(true);
	} else {
		return(false);
	}
}

//Lights up all the LEDs which represent your player's ships
void displayMyShips() {
	for (int i=0; i<MAXSHIPS; i++) {
		if (!shipDestroyed[myPlayer][i]) {
			digitalWrite(ledPin[shipLocation[myPlayer][i]], HIGH);
		}
	}
}

//Lights up the LEDs in a circle while playing the win sound when a player has won
void lightDisplay(){
	for(int i=0; i<8; i++) {
		int initialGame = speedInitial[i];
		for(int i=0; i<8; i++) {
		  	digitalWrite(ledPin[i], HIGH);
		  	delay(initialGame);
		  	digitalWrite(ledPin[i], LOW);
		  	tone(SPEAKERPIN, winTones[i]);
		}
	}
	delay(1000);
	noTone(SPEAKERPIN);
}



//   -----------------Define Sound Functions-----------------------------

// Define a function to play a sound that will be implemented by the missleFired and exploded functions.
void playSound(long frequency, long lengthSound){
  long period = 1000000/frequency;
  lengthSound = lengthSound*1000/period;
  for(long i = 0; i < lengthSound; i++){
    digitalWrite(SPEAKERPIN,HIGH);
    delayMicroseconds(period/2);
    digitalWrite(SPEAKERPIN,LOW);
    delayMicroseconds(period/2);
  }
}

// Define a function that uses the playSound one to play the sound of a missle that has been fired
void missleFired(){
	for(int i = 99; i < 1002; i += 5){
    playSound(1000000/i,10);
  }
}

// Define a function that uses the playSound one to play the sound of a ship that exlodes.
void exploded(int ship) {
	for(int k = 0; k < 250; k++){
	    long blow1 = random(100,2000);
	    playSound(blow1,3);
	    digitalWrite(ledPin[ship], !digitalRead(ledPin[ship]));
  	}  
}

// Define a function to play a succeeding sound when a player wins the game.
void succeedSound(){
	tone(SPEAKERPIN, 329);
	delay(300);
	noTone(SPEAKERPIN);
	tone(SPEAKERPIN, 494);
	delay(300);
	noTone(SPEAKERPIN);
	tone(SPEAKERPIN, 658);
	delay(300);
	noTone(SPEAKERPIN);
	delay(500);
}



//   -----------------Define Debugging Functions-----------------------------

//Prints out an error message to the debug serial monitor if errorMode is set at the top of the file
void err(char* message) {
	if (errorMode) {
		Serial.print("ERROR: ");
		Serial.println(message);
	}
}

//Prints out a warning message to the debug serial monitor if warningMode is set at the top of the file
void warn(char* message) {
	if (warningMode) {
		Serial.print("WARNING: ");
		Serial.println(message);
	}
}

//Prints out a debug message to the debug serial monitor if debugMode is set at the top of the file
void deb(char* message) {
	if (debugMode) {
		Serial.print("DEBUG: ");
		Serial.println(message);
	}
}

//Prints out a debug message following by a specified integer to the debug serial monitor if debugMode is set at the top of the file
void deb(char* message, int integer) {
	if (debugMode) {
		Serial.print("DEBUG: ");
		Serial.print(message);
		Serial.print(": ");
		Serial.println(integer);
	}
}