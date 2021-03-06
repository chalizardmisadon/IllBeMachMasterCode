//Start of Import libraries ==============================================================================
//BMP library --------------------
#include <Wire.h>                 //Wire library needed for I2C communication
#include <MacRocketry_BMP_180.h>  //Sparkfun modified library for BMP180

//GPS library --------------------
//#define Include_GPS //comment out this line to NOT use GPS
#ifdef Include_GPS
#include <MacRocketry_GPS_Shield.h>     //Jerry's library for the GPS shield
#endif

//SD Card library --------------------
#include <SPI.h>  //native library for serial peripheral interfacing (sd shield)
#include <SD.h>   //native library for SD reading nd writing
#include <MacRocketry_SD_Logger.h>      //SD Logger library

//LED Indicator library --------------------
#include <MacRocketry_LED_Indicator.h>  //Status indicator library

//Serial Debug --------------------
#define SERIAL_DEBUG

//End of Import libraries ================================================================================
//Start of #define =======================================================================================
//Data Process constant ----------------------------------------
#define Linear_Interpolate //comment out to not use linearInterpolate
#define sensorAltWeight (0.6)
#define Moving_Average

//Velocity constant (not using in IllBeMach)
//#define velTakeOff (0.02) //meter per millisecond = 20 m/s
//#define velDecentDrogue (-0.01) //meter per millisecond

//Timer constant
#define minTimeToApogee_K (24000) //millisecond = 24 seconds from preflight to apogee
#define maxTimeToDrogue_K (28000) //millisecond = 28 seconds from preflight to drogue
#define maxTimeToMain_K (135000) //milliseconds = 135 seconds from preflight to main

//Altitude constant
#define altTakeOff_K (15) //meter from preflight to flight
#define altMainMax_K (450) //meter which main is allowed to deploy

//Logger constant
#define decimal (7) //decimal places

//Chute IO Pins ==============================
#define drogueChutePin 2
#define mainChutePin 4

//End of #define =========================================================================================
//Start of Initialize Sensor Objects =====================================================================
#ifdef Include_GPS
MacRocketry_GPS_Shield gps;
#endif

MacRocketry_BMP_180 bmp;
MacRocketry_SD_Logger sd;
MacRocketry_LED_Indicator led;

float alt[5], vel[2];
uint32_t t[5];

#ifdef Moving_Average
float altSum, altRaw[10];
#endif

float altGndLevel, altMainMax;
uint32_t minTimeToApogee, maxTimeToDrogue, maxTimeToMain;


//Start of Data Acquisition functions --------------------
void readSensorData(void){
#ifdef Include_GPS
	if (gps.readSerialBuffer()){
		sd.writeBuffer(gps.getData());
	}
#endif
	
	if (bmp.readData()){
		sd.writeBuffer( //log BMP data
			"$BMP,temp," + String(bmp.getTemperature(),decimal) +
			",press," + String(bmp.getPressure(),decimal) +
			",alt," + String(bmp.getAltitude(),decimal) +
			"\n");
		
		//read time
		for (unsigned i = (sizeof(t)/sizeof(*t))-1; 0 < i; i--) t[i] = t[i-1];
		t[0] = bmp.getTime();
		float dT = (float)(t[0] - t[(sizeof(t)/sizeof(*t))-1]);
		
		//read altitude
		for (unsigned i = (sizeof(alt)/sizeof(*alt)-1); 0 < i; i--) alt[i] = alt[i-1];
		
		#ifdef Moving_Average //moving average altitude processing -------------------------
		for (unsigned i = (sizeof(altRaw)/sizeof(*altRaw)-1); 0 < i; i--) altRaw[i] = altRaw[i-1];
		altRaw[0] = bmp.getAltitude();
		altSum -= altRaw[(sizeof(altRaw)/sizeof(*altRaw))-1];  //minus last data point
		altSum += altRaw[0];                  //plus new data point
		
		alt[0] = altSum / (sizeof(altRaw)/sizeof(*altRaw));   //calculate average
		
		#else //raw data, no moving average processing ------------------------
		alt[0] = bmp.getAltitude();
		#endif
		
		#ifdef Linear_Interpolate //linear interpolate processing -------------------------
		float altPredict = alt[1] + vel[0] * (dT/ (float)(sizeof(t)/sizeof(*t)) );
		alt[0] = ( sensorAltWeight*alt[0] ) + ( (1.0-sensorAltWeight)*altPredict ); //re_calculate alt[0] for linear interpolate
		#endif
		
		//read velocity
		vel[1] = vel[0];
		vel[0] = (alt[0] - alt[(sizeof(alt)/sizeof(*alt))-1]) / dT;
		
		sd.writeBuffer( //log UNO calculated data
			"$UNO,t," + String(t[0]) +
			",alt," + String(alt[0],decimal) +
			//",dT," + String(dT,decimal) +
			",vel," + String(vel[0],decimal) +
			"\n");
			
	}
}

//End of Data Acquisition functions --------------------
//End of Initialize Sensor Objects =======================================================================
//Start of Rocket State Machine ==========================================================================
//Rocket State Machine enum and variables --------------------
enum RocketState {
	Init,
	Preflight,
	Flight,
	DrogueChute,
	MainChute,
	Landed
};
RocketState currentState = Init;

//Start of Rocket State Machine functions --------------------
void nextRocketState(RocketState state); //Arduino IDE bugs out when passing enum parameter
void nextRocketState(RocketState state){
	if (currentState == state){ //extra redundancy
		switch (currentState){
			case Init: {
				sd.writeBuffer("@Preflight\n");
				led.setRGB(0, 255, 0);
				delay(1500);
				currentState = Preflight;
			} break;

			case Preflight: {
				sd.writeBuffer("@Flight\n");
				led.setRGB(127, 255, 0);
				currentState = Flight;
			} break;

			case Flight: {
				sd.writeBuffer("@DrogueChute\n");
				led.setRGB(255, 140, 0);
				currentState = DrogueChute;
			} break;

			case DrogueChute: {
				sd.writeBuffer("@MainChute\n");
				led.setRGB(255, 165, 0);
				currentState = MainChute;
			} break;

			case MainChute: {
				sd.writeBuffer("@Landed\n");
				currentState = Landed;
			} break;

			default: break;
		}
	}
}

void processRocketState(){
	readSensorData(); //read all sensor data
	
	switch (currentState){ //process state
		case Init:				{ rocketInit();					} break;
		case Preflight: 	{ rocketPreflight();		} break;
		case Flight: 			{	rocketFlight();				} break;
		case DrogueChute:	{ rocketDrogueChute();	} break;
		case MainChute:		{ rocketMainChute();		} break;
		case Landed:			{ rocketLanded();				} break;
		default: break;
	}
}
//End of Rocket State Machine functions --------------------
//Start of Rocket State-Specific functions --------------------
void rocketInit(){ //initialize all variables to zero
	sd.writeBuffer("@Init\n");
	led.setRGB(255, 0, 0);
	delay(1500);
	
#ifdef Include_GPS
	led.setStatusGPS(gps.getFix());
#endif
	led.setStatusBMP(bmp.getConnectBMP());
	led.setStatusSD(sd.getConnectFile());
	
	if (!bmp.getConnectBMP()) while (1);  //stop rocket
	if (!sd.getConnectFile()) while (1);  //stop rocket
	
	bmp.setOversampling(3); //set oversampling to max
	
	for (unsigned i = 0; i < sizeof(vel)/sizeof(*vel); i++) vel[i] = 0;
	for (unsigned i = 0; i < sizeof(alt)/sizeof(*alt); i++){ //read in first sizeof_alt_array data points
		if (bmp.readData()){
			sd.writeBuffer( //log BMP data
				"$BMP,temp," + String(bmp.getTemperature(),decimal) +
				",press," + String(bmp.getPressure(),decimal) +
				",alt," + String(bmp.getAltitude(),decimal) +
				"\n");
			
			//read time
			for (unsigned i = (sizeof(t)/sizeof(*t)-1); 0 < i; i--) t[i] = t[i-1];
			t[0] = bmp.getTime();
			
			//read altitude
			for (unsigned i = (sizeof(t)/sizeof(*t)-1); 0 < i; i--) alt[i] = alt[i-1];
			alt[0] = bmp.getAltitude();
			
		}
	}
	
#ifdef Moving_Average //moving average altitude processing -------------------------
	altSum = 0;
	for (unsigned i = 0; i < sizeof(alt)/sizeof(*alt); i++) altSum += alt[i];
	float altAvg = altSum / (sizeof(alt)/sizeof(*alt));
	for (unsigned i = 0; i < sizeof(altRaw)/sizeof(*altRaw); i++) altRaw[i] = altAvg; //fill array with avg
	altSum = altAvg * sizeof(altRaw)/sizeof(*altRaw);
#endif
	
	//get altGndLevel by averaging all past alt measurement
	float sum = 0;
	for (unsigned i = 0; i < sizeof(alt)/sizeof(*alt); i++) sum += alt[i];
	altGndLevel = sum / (sizeof(alt)/sizeof(*alt)); //get last altitude reading
	
	nextRocketState(Init);
}

void rocketPreflight(){
	//calculate slow drifting Ground Altitude
	altGndLevel = (altGndLevel*0.8) + (0.2*alt[(sizeof(alt)/sizeof(*alt))-1]); //weighted altitude reading
	float altTakeOff = altGndLevel + altTakeOff_K; //set takeoff relative to GndLevel
		
	if ((altTakeOff < alt[1]) &&  //if altitude is higher than takeoff
			(altTakeOff < alt[0]))
	{ //then rocket is in flight

		//set all timer
		minTimeToApogee = millis() + minTimeToApogee_K; //set apogeee timer
		maxTimeToDrogue = millis() + maxTimeToDrogue_K; //set drogue timer
		maxTimeToMain = millis() + maxTimeToMain; //set main timer

		//set altitude
		altMainMax = altGndLevel + altMainMax_K; //set IREC 450m
		
		sd.writeBuffer( //log UNO calculated data
			"$Pre,tApogee," + String(minTimeToApogee) +
			",tDrogue," + String(maxTimeToDrogue) +
			",tMain," + String(maxTimeToMain) +
			",altGND," + String(altGndLevel,decimal) +
			",altMain," + String(altMainMax,decimal) +
			"\n");
		
		bmp.setOversampling(0); //set oversampling to fastest
		nextRocketState(Preflight);
	}
}

void rocketFlight(){
	//wait for apogee timer to elapsed and switch to drogue state
	//state must NOT detonate Drogue Chute for this length of time
	if (minTimeToApogee < millis()){
		nextRocketState(Flight);
	}
}

void rocketDrogueChute(){
	//detonate DrogueChute when either altitude decrease or drogue timer elapsed
	
	if (((alt[0] - alt[(sizeof(alt)/sizeof(*alt))-1]) < 0)	||	//altitude decrease
			(maxTimeToDrogue < millis()))														//drogue timer elapsed
	{
#if 0
		sd.writeBuffer( //log UNO calculated data
			"$Drogue,altDelta," + String((alt[0] - alt[(sizeof(alt)/sizeof(*alt))-1]),decimal) +
			",t," + String(millis()) +
			"\n");
#endif
		
		digitalWrite(drogueChutePin, HIGH); //detonate Drogue
		nextRocketState(DrogueChute);
	}
}

void rocketMainChute(){
	//detonate MainChute when either altitude below IREC 450m or main timer elapsed
	
	if ((alt[0] < altMainMax) ||				//altitude is below IREC 405m
			(maxTimeToMain < millis()))			//main timer elapsed minTimeToMain elapsed
	{
#if 1
		sd.writeBuffer( //log UNO calculated data
			"$Main," + String(millis()) +
			"\n");
#endif
		
		digitalWrite(mainChutePin, HIGH); //detonate Main Chute
		nextRocketState(MainChute);
	}
}

void rocketLanded(){
#if 0
	if ( //if altitude change is within 3m
		((alt[0] - alt[(sizeof(alt)/sizeof(*alt))-1]) < 3)  &&
		((alt[(sizeof(alt)/sizeof(*alt))-1] - alt[0]) < 3)
	){
		sd.writeBuffer( //log UNO calculated data
			"$Landed\n");
	}
#endif
}

//End of Rocket State-Specific functions --------------------
//End of Rocket State Machine ============================================================================
//Start of setup() and loop() ============================================================================
void setup(){ //redundancy initialize all variables to safety
#ifdef SERIAL_DEBUG
	Serial.begin(9600);
	while(!Serial);
#endif

	bmp.begin(); //need to initialize BMP

	//set all motor driver pin to low
	pinMode(drogueChutePin, OUTPUT);
	pinMode(mainChutePin, OUTPUT);
	digitalWrite(drogueChutePin, LOW);
	digitalWrite(mainChutePin, LOW);

	//set minTimeToAll to very large value for safety
	minTimeToApogee = 3600000; //set min time to 1 hour
	maxTimeToDrogue = 3600000; //set min time to 1 hour
	maxTimeToMain = 3600000; //set min time to 1 hour

	//set altMainMax to very low for safety
	altMainMax = 0;
	altGndLevel = 0;
	
	currentState = Init; //reset current state to Init
	bmp.setOversampling(3); //set init oversampling to max
}

void loop(){
#ifdef SERIAL_DEBUG
	Serial.println("Hello World");
#endif
	processRocketState();
}


//End of setup() and loop() ==============================================================================
