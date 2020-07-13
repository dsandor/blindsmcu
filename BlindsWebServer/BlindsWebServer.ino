#include <async_config.h>
#include <AsyncPrinter.h>
#include <DebugPrintMacros.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncTCPbuffer.h>
#include <SyncClient.h>
#include <tcp_axtls.h>

#define DEVICE_NAME "left office binds"
#define DEBUG_FAUXMO Serial
#define DEBUG_FAUXMO_VERBOSE_TCP 1
#define DEBUG_FAUXMO_VERBOSE_UDP 1
#define AH_DRIVER 0

#include <fauxmoESP.h>
#include <templates.h>

#include "SimpleTimer.h"    //https://github.com/marcelloromani/Arduino-SimpleTimer/tree/master/SimpleTimer
#include <ESP8266WiFi.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <ESP8266mDNS.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <PubSubClient.h>   //https://github.com/knolleary/pubsubclient
#include <ArduinoOTA.h>     //https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA

#if AH_DRIVER == 1
#include <AH_EasyDriver.h>  //http://www.alhin.de/arduino/downloads/AH_EasyDriver_20120512.zip
#else
#include <AccelStepper.h> // http://www.airspayce.com/mikem/arduino/AccelStepper/
#endif

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESPAsyncWebServer.h>    //Local WebServer used to serve the configuration portal
#include <ESPAsyncWiFiManager.h>  //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <AsyncJson.h>
#include <ArduinoJson.h>

// Web Server Notes: https://techtutorialsx.com/2016/10/22/esp8266-webserver-getting-query-parameters/
// Wifi Connection Manager: https://tzapu.com/esp8266-wifi-connection-manager-library-arduino-ide/

#define IN1 D1 // blue
#define IN2 D2 // pink
#define IN3 D5 // yellow
#define IN4 D6 // orange


#define STEPPER_SPEED             50                  //Defines the speed in RPM for your stepper motor
#define STEPPER_STEPS_PER_REV     1028                //Defines the number of pulses that is required for the stepper to rotate 360 degrees
#define STEPPER_MICROSTEPPING     0                   //Defines microstepping 0 = no microstepping, 1 = 1/2 stepping, 2 = 1/4 stepping 
#define DRIVER_INVERTED_SLEEP     1                   //Defines sleep while pin high.  If your motor will not rotate freely when on boot, comment this line out.
#if AH_DRIVER == 1
#define STEPS_TO_CLOSE            16                  //Defines the number of steps needed to open or close fully
#else
#define STEPS_TO_CLOSE 1000 //Defines the number of steps needed to open or close fully
#endif

#define STEPPER_DIR_PIN           D6
#define STEPPER_STEP_PIN          D7
#define STEPPER_SLEEP_PIN         D5

#define STEPPER2_DIR_PIN           D1
#define STEPPER2_STEP_PIN          D2
#define STEPPER2_SLEEP_PIN         D0

#define STEPPER_MICROSTEP_1_PIN   14
#define STEPPER_MICROSTEP_2_PIN   12
 
// Set web server port number to 80
AsyncWebServer server(80);
fauxmoESP fauxmo;
DNSServer dns;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

SimpleTimer timer;
bool stepperPinsEnabled = false;

#if AH_DRIVER == 1
AH_EasyDriver stepper1(STEPPER_STEPS_PER_REV, STEPPER_DIR_PIN, STEPPER_STEP_PIN, STEPPER_MICROSTEP_1_PIN, STEPPER_MICROSTEP_2_PIN, STEPPER_SLEEP_PIN);
AH_EasyDriver stepper2(STEPPER_STEPS_PER_REV, STEPPER2_DIR_PIN, STEPPER2_STEP_PIN, STEPPER_MICROSTEP_1_PIN, STEPPER_MICROSTEP_2_PIN, STEPPER2_SLEEP_PIN);

AH_EasyDriver steppers[2] = {stepper1, stepper2};
#else
AccelStepper stepper(4, IN1, IN3, IN2, IN4);
#endif

String webui = "<!doctypehtml><link rel=\"stylesheet\"href=\"https://unpkg.com/@blaze/css@9.2.0/dist/blaze/blaze.css\"><script type=\"module\"src=\"https://unpkg.com/@blaze/atoms@9.2.0/dist/blaze-atoms/blaze-atoms.esm.js\"></script><script nomodule=\"\"src=\"https://unpkg.com/@blaze/atoms@9.2.0/dist/blaze-atoms/blaze-atoms.js\"></script><meta name=\"viewport\"content=\"width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,minimal-ui\"><title>Blinds Controller</title><div class=\"c-card u-text\"><header class=\"c-card__header\"><h2 class=\"c-heading\">{{name}}<div class=\"c-heading__sub\">Current Position: {{current_position}}</div></h2></header><div class=\"c-card__body\"></div><footer class=\"c-card__footer\"><form action=\"/blinds\"><div class=\"c-input-group\"><input value=\"\"name=\"position\">&nbsp; <input type=\"hidden\"value=\"0\"name=\"blind\"> <input type=\"submit\"value=\"Set Position\"> <a href=\"/blinds?blind=0&position=1000&calibrate-closed=true\"style=\"margin:10px\"><blaze-badge ghost rounded>Recalibrate Closed</blaze-badge></a></div></form><div class=\"c-input-group\"><a href=\"/blinds?blind=0&position=0\"style=\"margin:10px\"><blaze-badge ghost rounded>Close</blaze-badge></a><a href=\"/blinds?blind=0&position=1000\"style=\"margin:10px\"><blaze-badge ghost rounded>Open</blaze-badge></a></div></footer></div>";

bool boot = true;
int currentPosition [] = { 0, 0 };
int newPosition [] = { 0, 0 };
bool moving [] = { false, false };

char deviceName[20];
int32 processCounter = 0;

std::vector<std::string> split(std::string str, std::string sep)
{
  char *cstr = const_cast<char *>(str.c_str());
  char *current;
  std::vector<std::string> arr;
  current = strtok(cstr, sep.c_str());
  while (current != NULL)
  {
    arr.push_back(current);
    current = strtok(NULL, sep.c_str());
  }
  return arr;
}

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
  // server.on("/binds", HTTP_GET, [](AsyncWebServerRequest *request) { //Handler
  //   String message = "Number of args received:";
  //   message += request->args(); //Get number of parameters
  //   message += "\n";          //Add a new line

  //   //List all parameters (Compatibility)
  //   int args = request->args();
  //   for (int i = 0; i < args; i++)
  //   {
  //     Serial.printf("ARG[%s]: %s\n", request->argName(i).c_str(), request->arg(i).c_str());
  //   }

  //   int pos = request->arg("position").toInt();
  //   int blind_no = request->arg("blind").toInt();

  //   newPosition[blind_no] = pos;

  //   Serial.print("Setting new position:");
  //   Serial.println(pos);
    
  //   request->send(200, "text/plain", message); //Response to the HTTP request
  // });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { //Handler
    String message = "root called:";
    message += request->url(); //Get number of parameters
    message += "\n";            //Add a new line

    Serial.println(message);
    String response = webui;
    
    response.replace("{{name}}", DEVICE_NAME);
#if AH_DRIVER == 1
    response.replace("{{current_position}}", currentPosition);
#else
    response.replace("{{current_position}}", String(stepper.currentPosition()));
#endif
    request->send(200, "text/html", response); //Response to the HTTP request
  });

  server.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("caught description.xml request.");

    String message = fauxmo.getDescriptionXml();

    Serial.println(message);

    request->send(200, "text/xml", message);
  });

  server.on("/api", HTTP_POST, [](AsyncWebServerRequest *request) {
    String message = "[{\"success\":{\"username\":\"c6260f982b43a226b5542b967f612ce\"}}]\n\n";

    Serial.println("POST /api");
    Serial.print("URL: ");
    Serial.println(request->url());

    AsyncClient *client = request->client();
    String ip = client->remoteIP().toString();

    Serial.print("Remote IP: ");
    Serial.println(ip);

    //List all parameters (Compatibility)
    int args = request->args();
    for (int i = 0; i < args; i++)
    {
      Serial.printf("ARG[%s]: %s\n", request->argName(i).c_str(), request->arg(i).c_str());
    }

    //List all collected headers
    int headers = request->headers();
    int i;
    for (i = 0; i < headers; i++)
    {
      AsyncWebHeader *h = request->getHeader(i);
      Serial.printf("HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    Serial.println(message);

    request->send(200, "application/json", message);
  });

  AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/api", [](AsyncWebServerRequest *request, JsonVariant &json) {
    Serial.println("json callback called..");
    const JsonObject &jsonObj = json.as<JsonObject>();
    String url = request->url();
    Serial.print("url: ");
    Serial.println(url);

    std::vector<std::string> urlParts;

    urlParts = split(url.c_str(), "/");

    Serial.printf("Url parts size: %d ", urlParts.size());
    Serial.printf("Url position 3: %s\n", urlParts[3].c_str());

    if (urlParts.size() < 5)
      return;

    Serial.println("converting id.");
    Serial.println(urlParts[3].c_str());

    // Get the id
    int id = String(urlParts[3].c_str()).toInt();

    Serial.printf("Converted to: %d", id);
    Serial.printf("%s called.. got json. device id: %d\n", url.c_str(), id);

    String jsonString;
    serializeJsonPretty(jsonObj, jsonString);

    Serial.println(jsonString);

    String response = fauxmo.getStateResponse(id - 1, jsonObj["on"]);
    Serial.println("State Response:");
    Serial.println(response);

    request->send(200, "application/json", response);
  });

  // server.addHandler(handler);

  // These two callbacks are required for gen1 and gen3 compatibility
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.println("onRequestBody called.");

    if (fauxmo.process(request->client(), request->method() == HTTP_GET, request->url(), String((char *)data)))
      return;
    // Handle any other body request here...
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    Serial.println("Not found called.");

    if (request->url() == "/blinds") {
      processBlindsApi(request);
    }

      String body = (request->hasParam("body", true)) ? request->getParam("body", true)->value() : String();
    if (fauxmo.process(request->client(), request->method() == HTTP_GET, request->url(), body))
      return;
    // Handle not found request here...
  });

  server.begin();
}

void setup_ota() {
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

void processBlindsApi(AsyncWebServerRequest *request)
{
  Serial.println("processBlindsApi called.");

  String message = "Number of args received:";
  message += request->args(); //Get number of parameters
  message += "\n";            //Add a new line

  //List all parameters (Compatibility)
  int args = request->args();
  for (int i = 0; i < args; i++)
  {
    Serial.printf("ARG[%s]: %s\n", request->argName(i).c_str(), request->arg(i).c_str());
  }

  int pos = request->arg("position").toInt();
  int blind_no = request->arg("blind").toInt();

  if (request->hasArg("calibrate-closed")) {
#if AH_DRIVER == 0
    stepper.setCurrentPosition(0);
#else
    currentPosition = 0;
    newPosition[blind_no] = 0;
#endif
    request->redirect("/");
    return;
  }

  Serial.print("Setting new position: ");
  Serial.println(pos);

  stepperPinsEnabled = true;
#if AH_DRIVER == 0
  stepper.enableOutputs();
#endif

  newPosition[blind_no] = pos;

  request->redirect("/");
}

void setup() {
  Serial.begin(115200);

#if AH_DRIVER == 1
  steppers[0].setMicrostepping(STEPPER_MICROSTEPPING);            // 0 -> Full Step                                
  steppers[0].setSpeedRPM(STEPPER_SPEED);     // set speed in RPM, rotations per minute

  steppers[1].setMicrostepping(STEPPER_MICROSTEPPING);            // 0 -> Full Step                                
  steppers[1].setSpeedRPM(STEPPER_SPEED);     // set speed in RPM, rotations per minute
#endif

#if DRIVER_INVERTED_SLEEP == 1 && AH_DRIVER == 1
  steppers[0].sleepOFF();
  steppers[1].sleepOFF();
#endif

#if DRIVER_INVERTED_SLEEP == 0 && AH_DRIVER == 1
  steppers[0].sleepON();
  steppers[1].sleepON();
#endif

#if AH_DRIVER == 0
  stepper.setMaxSpeed(1000);
  stepper.setSpeed(650);
#endif

  // use wifi manager instead of hard coding credentials.
  AsyncWiFiManager wifiManager(&server, &dns);

  sprintf(deviceName, "BLINDS_%08X", ESP.getChipId());
  
  if(!wifiManager.autoConnect(deviceName)) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  } 
  
  WiFi.mode(WIFI_STA);
  setup_wifi();
  setup_ota();
  
  // Setup Fauxmo for alexa support
  fauxmo.createServer(false);
  fauxmo.setPort(80); // This is required for gen3 devices
  fauxmo.enable(true);
  fauxmo.addDevice(DEVICE_NAME);
  //fauxmo.addDevice("blinds right");

  delay(10);
  Serial.print("interval: ");
  Serial.println(((1 << STEPPER_MICROSTEPPING)*5800)/STEPPER_SPEED);
  // timer.setInterval(((1 << STEPPER_MICROSTEPPING)*5800)/STEPPER_SPEED, processSteppers);

  fauxmo.onSetState([](unsigned char device_id, const char *device_name, bool state, unsigned char value) {
    Serial.printf("[MAIN] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);
    String strValue = String(value);
    float alexa_value = atof(strValue.c_str());
    double percent = alexa_value / 256.0f;
    int computed = floor(STEPS_TO_CLOSE * percent);

    newPosition[device_id] = computed;
  });
}

void loop(){
  //timer.run();
  fauxmo.handle();
  processSteppers();
  ArduinoOTA.handle();
}

#if AH_DRIVER == 1
void processSteppers() {
  processStepper(0);
  processStepper(1);
}

void processStepper(int stepperNumber)
{
  if (newPosition[stepperNumber] > currentPosition[stepperNumber])
  {    
    Serial.println("Moving stepper.. newPosition > currentPosition");
    #if DRIVER_INVERTED_SLEEP == 1
    steppers[stepperNumber].sleepON();
    #endif
    #if DRIVER_INVERTED_SLEEP == 0
    steppers[stepperNumber].sleepOFF();
    #endif
    steppers[stepperNumber].move(80, FORWARD);
    currentPosition[stepperNumber]++;
    moving[stepperNumber] = true;
  }
  if (newPosition[stepperNumber] < currentPosition[stepperNumber])
  {
    Serial.println("Moving stepper.. newPosition  < currentPosition");
    #if DRIVER_INVERTED_SLEEP == 1
    steppers[stepperNumber].sleepON();
    #endif
    #if DRIVER_INVERTED_SLEEP == 0
    steppers[stepperNumber].sleepOFF();
    #endif
    steppers[stepperNumber].move(80, BACKWARD);
    currentPosition[stepperNumber] --;
    moving[stepperNumber] = true;
  }
  if (newPosition[stepperNumber] == currentPosition[stepperNumber] && moving[stepperNumber] == true)
  {
    Serial.println("Stopping stepper.");
    #if DRIVER_INVERTED_SLEEP == 1
    steppers[stepperNumber].sleepOFF();
    #endif
    #if DRIVER_INVERTED_SLEEP == 0
    steppers[stepperNumber].sleepON();
    #endif
    moving[stepperNumber] = false;
  }
}
#else
void processSteppers()
{
  processStepper();
}
void processStepper()
{
  int stepperNumber = 0;
  
  /*
  if (processCounter > 5000)
  {
    processCounter = 0;
    Serial.print("distanceToGo: ");
    Serial.print(stepper.distanceToGo());
    Serial.print(", currentPosition: ");
    Serial.print(stepper.currentPosition());
    Serial.print(", isRunning: ");
    Serial.print(stepper.isRunning());
    Serial.print(", newPosition[stepperNumber]: ");
    Serial.print(newPosition[stepperNumber]);
    Serial.println("");
  }
  */

  if (stepper.currentPosition() != newPosition[stepperNumber])
  {
    //Serial.println("Moving stepper.");
    stepper.moveTo(newPosition[stepperNumber]);
    stepper.setSpeed(650);
  }

  if (stepper.distanceToGo() == 0 && stepperPinsEnabled) {
    stepper.disableOutputs();
    stepperPinsEnabled = false;
  }

  stepper.runSpeedToPosition();
}
#endif