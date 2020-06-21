#include "SimpleTimer.h"    //https://github.com/marcelloromani/Arduino-SimpleTimer/tree/master/SimpleTimer
#include <ESP8266WiFi.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <ESP8266mDNS.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <PubSubClient.h>   //https://github.com/knolleary/pubsubclient
#include <ArduinoOTA.h>     //https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA
#include <AH_EasyDriver.h>  //http://www.alhin.de/arduino/downloads/AH_EasyDriver_20120512.zip
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

// Web Server Notes: https://techtutorialsx.com/2016/10/22/esp8266-webserver-getting-query-parameters/
// Wifi Connection Manager: https://tzapu.com/esp8266-wifi-connection-manager-library-arduino-ide/

/*****************  START USER CONFIG SECTION *********************************/
/*****************  START USER CONFIG SECTION *********************************/
/*****************  START USER CONFIG SECTION *********************************/
/*****************  START USER CONFIG SECTION *********************************/

#define STEPPER_SPEED             50                  //Defines the speed in RPM for your stepper motor
#define STEPPER_STEPS_PER_REV     1028                //Defines the number of pulses that is required for the stepper to rotate 360 degrees
#define STEPPER_MICROSTEPPING     0                   //Defines microstepping 0 = no microstepping, 1 = 1/2 stepping, 2 = 1/4 stepping 
#define DRIVER_INVERTED_SLEEP     1                   //Defines sleep while pin high.  If your motor will not rotate freely when on boot, comment this line out.

#define STEPS_TO_CLOSE            16                  //Defines the number of steps needed to open or close fully

#define STEPPER_DIR_PIN           D6
#define STEPPER_STEP_PIN          D7
#define STEPPER_SLEEP_PIN         D5

#define STEPPER2_DIR_PIN           D0
#define STEPPER2_STEP_PIN          D1
#define STEPPER2_SLEEP_PIN         D2

#define STEPPER_MICROSTEP_1_PIN   14
#define STEPPER_MICROSTEP_2_PIN   12
 
/*****************  END USER CONFIG SECTION *********************************/
/*****************  END USER CONFIG SECTION *********************************/
/*****************  END USER CONFIG SECTION *********************************/
/*****************  END USER CONFIG SECTION *********************************/
/*****************  END USER CONFIG SECTION *********************************/

/** \/ Web Server Config Section */

// Set web server port number to 80
ESP8266WebServer server(80);

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
String output5State = "open";
String output4State = "open";

// Assign output variables to GPIO pins
const int output5 = 5;
const int output4 = 4;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;
/** /\ Web Server Config Section */


SimpleTimer timer;
AH_EasyDriver shadeStepper(STEPPER_STEPS_PER_REV, STEPPER_DIR_PIN ,STEPPER_STEP_PIN,STEPPER_MICROSTEP_1_PIN,STEPPER_MICROSTEP_2_PIN,STEPPER_SLEEP_PIN);
AH_EasyDriver shadeStepper2(STEPPER_STEPS_PER_REV, STEPPER2_DIR_PIN ,STEPPER2_STEP_PIN,STEPPER_MICROSTEP_1_PIN,STEPPER_MICROSTEP_2_PIN,STEPPER2_SLEEP_PIN);

//Global Variables (I did some copy pasting for the second stepper.. DRY this code.)
bool boot = true;
int currentPosition = 0;
int newPosition = 0;

int currentPosition2 = 0;
int newPosition2 = 0;

char positionPublish[50];
bool moving = false;
bool moving2 = false;

char deviceName[20];

//Functions
void setup_wifi() {
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("setup_wifi called..");

  /*Serial.println(ssid);

  WiFi.begin(ssid, password);
*/
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // After WIFI is setup we configure the WebServer.
  server.on("/binds", handleBlindsAction);
  server.begin();
}

//Run once setup
void setup() {
  Serial.begin(115200);
  shadeStepper.setMicrostepping(STEPPER_MICROSTEPPING);            // 0 -> Full Step                                
  shadeStepper.setSpeedRPM(STEPPER_SPEED);     // set speed in RPM, rotations per minute

  shadeStepper2.setMicrostepping(STEPPER_MICROSTEPPING);            // 0 -> Full Step                                
  shadeStepper2.setSpeedRPM(STEPPER_SPEED);     // set speed in RPM, rotations per minute
  
  #if DRIVER_INVERTED_SLEEP == 1
  shadeStepper.sleepOFF();
  shadeStepper2.sleepOFF();
  #endif

  #if DRIVER_INVERTED_SLEEP == 0
  shadeStepper.sleepON();
  shadeStepper2.sleepON();
  #endif

  // use wifi manager instead of hard coding credentials.
  WiFiManager wifiManager;

  sprintf(deviceName, "BLINDS_%08X", ESP.getChipId());
  
  if(!wifiManager.autoConnect(deviceName)) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  } 
  
  WiFi.mode(WIFI_STA);
  setup_wifi();
  
  delay(10);
  Serial.print("interval: ");
  Serial.println(((1 << STEPPER_MICROSTEPPING)*5800)/STEPPER_SPEED);
  timer.setInterval(((1 << STEPPER_MICROSTEPPING)*5800)/STEPPER_SPEED, processSteppers);   
}

void handleBlindsAction() { //Handler
  String message = "Number of args received:";
  message += server.args();            //Get number of parameters
  message += "\n";                            //Add a new line

  for (int i = 0; i < server.args(); i++) {

    message += "Arg :" + (String)i + " -> ";   //Include the current iteration value
    message += server.argName(i) + ": ";     //Get the name of the parameter
    message += server.arg(i) + "\n";              //Get the value of the parameter
  } 

  int pos = server.arg("position").toInt();  
  newPosition = pos;
  
  server.send(200, "text/plain", message);       //Response to the HTTP request
}

void loop(){
  timer.run();
  server.handleClient();
}

void processSteppers() {
  processStepper();
  processStepper2();
}

void processStepper()
{
  if (newPosition > currentPosition)
  {
    Serial.println("Moving stepper.. newPosition > currentPosition");
    #if DRIVER_INVERTED_SLEEP == 1
    shadeStepper.sleepON();
    #endif
    #if DRIVER_INVERTED_SLEEP == 0
    shadeStepper.sleepOFF();
    #endif
    shadeStepper.move(80, FORWARD);
    currentPosition++;
    moving = true;
  }
  if (newPosition < currentPosition)
  {
    Serial.println("Moving stepper.. newPosition < currentPosition");
    #if DRIVER_INVERTED_SLEEP == 1
    shadeStepper.sleepON();
    #endif
    #if DRIVER_INVERTED_SLEEP == 0
    shadeStepper.sleepOFF();
    #endif
    shadeStepper.move(80, BACKWARD);
    currentPosition--;
    moving = true;
  }
  if (newPosition == currentPosition && moving == true)
  {
    Serial.println("Stopping stepper.");
    #if DRIVER_INVERTED_SLEEP == 1
    shadeStepper.sleepOFF();
    #endif
    #if DRIVER_INVERTED_SLEEP == 0
    shadeStepper.sleepON();
    #endif
    moving = false;
  }
}

void processStepper2()
{
  if (newPosition2 > currentPosition2)
  {
    Serial.println("Moving stepper 2.. newPosition2 > currentPosition2");
    #if DRIVER_INVERTED_SLEEP == 1
    shadeStepper2.sleepON();
    #endif
    #if DRIVER_INVERTED_SLEEP == 0
    shadeStepper2.sleepOFF();
    #endif
    shadeStepper2.move(80, FORWARD);
    currentPosition2++;
    moving2 = true;
  }
  if (newPosition2 < currentPosition2)
  {
    Serial.println("Moving stepper 2.. newPosition2 < currentPosition2");
    #if DRIVER_INVERTED_SLEEP == 1
    shadeStepper2.sleepON();
    #endif
    #if DRIVER_INVERTED_SLEEP == 0
    shadeStepper2.sleepOFF();
    #endif
    shadeStepper2.move(80, BACKWARD);
    currentPosition2 --;
    moving2 = true;
  }
  if (newPosition2 == currentPosition2 && moving2 == true)
  {
    Serial.println("Stopping stepper 2.");
    #if DRIVER_INVERTED_SLEEP == 1
    shadeStepper2.sleepOFF();
    #endif
    #if DRIVER_INVERTED_SLEEP == 0
    shadeStepper2.sleepON();
    #endif

    moving2 = false;
  }
}
