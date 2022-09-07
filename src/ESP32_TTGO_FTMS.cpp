/**
 *
 *
 * The MIT License (MIT)
 * Copyright © 2021, 2022 <Andreas Loeffler>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the “Software”), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define NIMBLE

// initially started with the sketch from:
// https://hackaday.io/project/175237-add-bluetooth-to-treadmill

// FIXME: incline manual/auto toggle -> not same for incline and speed!!
// FIXME: if manual incline round up/down to integer
// FIXME: elevation gain
// FIXME: once and for good ... camle vs. underscore case
// TODO:  web config menu (+https, +setup initial user password)

#include "common.h"

#include <SPIFFS.h>
#include <Preferences.h>
#include <unity.h>

#ifndef NIMBLE
// original
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h> // notify
#else
#include <NimBLEDevice.h>
#endif

//#include <algorithm>
#include <math.h>
#include <SPI.h>


#include <Button2.h>
#include <TimeLib.h>  // https://playground.arduino.cc/Code/Time/
#ifndef NO_MPU6050
#include <Wire.h>
#include <MPU6050_light.h> // accelerometer and gyroscope -> measure incline
#endif
#ifndef NO_VL53L0X
#include <VL53L0X.h>       // time-of-flight sensor -> get incline % from distance to ground
#endif

#include "GPIOExtenderAW9523.h"



// Select and uncomment one of the Treadmills below
//#define TREADMILL_MODEL TAURUS_9_5
//#define TREADMILL_MODEL NORDICTRACK_12SI
// or via platformio.ini:
// -DTREADMILL_MODEL="TAURUS_9_5"


const char* VERSION = "0.0.22_lvgl";

#if TARGET_TTGO_T_DISPLAY
// TTGO T-Display buttons
#define BUTTON_1 35
#define BUTTON_2  0
static const int SDA_0 = 21;
static const int SCL_0 = 22;
static const uint32_t I2C_FREQ = 400000;

#elif TARGET_WT32_SC01
// This is a touch screen so there is no buttons
// IO_21 and IO_22 are routed out and use used for SPI to the screen
// Pins with names that start with J seems to be connected so screen, take care to avoid.
static const int SDA_0 = 18;
static const int SCL_0 = 19;
static const uint32_t I2C_FREQ = 400000;
#ifndef NUM_TOUCH_BUTTONS
#define NUM_TOUCH_BUTTONS 6
#endif

/*
//LGFX_Button touchButtons[NUM_TOUCH_BUTTONS];
LGFX_Button btnSpeedToggle    = LGFX_Button();
LGFX_Button btnInclineToggle  = LGFX_Button();
LGFX_Button btnSpeedUp        = LGFX_Button();
LGFX_Button btnSpeedDown      = LGFX_Button();
LGFX_Button btnInclineUp      = LGFX_Button();
LGFX_Button btnInclineDown    = LGFX_Button();
*/
// 480 x 320, use full-top-half to show speed, incline, total_dist, and acc. elevation gain
// maybe draw some nice gauges for speed/incline and odometers for dist and elevation
// in addition might want to have an elevation profile being displayed
// 260, 5, 100, 50, TFT_WHITE, TFT_BLUE, TFT_WHITE, "MODE");
#define btnToggle_W           100
#define btnToggle_H            50

#define btnSpeedToggle_X       250
#define btnSpeedToggle_Y         5
#define btnInclineToggle_X     360
#define btnInclineToggle_Y       5
#define btnSpeedUp_X           250
#define btnSpeedUp_Y           100
#define btnSpeedDown_X         250
#define btnSpeedDown_Y         170
#define btnInclineUp_X         360
#define btnInclineUp_Y         100
#define btnInclineDown_X       360
#define btnInclineDown_Y       170


#elif TARGET_TTGO_T4
// no-touch screen with three buttons
#define BUTTON_1 38  // LEFT
#define BUTTON_2 37  // CENTRE
#define BUTTON_3 39  // RIGHT
#define SDA_0 21
#define SCL_0 22
#define I2C_FREQ 400000

#else
#error Unknown button setup
#endif


// PINS
// NOTE TTGO T-DISPAY GPIO 36,37,38,39 can only be input pins https://github.com/Xinyuan-LilyGO/TTGO-T-Display/issues/10

//#define SPEED_IR_SENSOR1   15
//#define SPEED_IR_SENSOR2   13
static constexpr int AW9523_INTERRUPT_PIN  = 25; // GPIO Extender interrupt
static constexpr int SPEED_REED_SWITCH_PIN = 26; // REED-Contact

volatile unsigned long t1;
volatile unsigned long t2;
volatile boolean t1_valid = false;
volatile boolean t2_valid = false;
volatile boolean touch_b1 = false;
volatile boolean touch_b2 = false;
volatile boolean touch_b3 = false;


uint8_t speedInclineMode = SPEED;
boolean hasMPU6050 = false;
boolean hasVL53L0X = false;
boolean hasIrSense = false;
boolean hasReed    = true;  // set to false if you don't have added to read Reed (for speed)

boolean setupDone  = false;

#define EVERY_SECOND 1000
#define WIFI_CHECK   30 * EVERY_SECOND
unsigned long sw_timer_clock = 0;
unsigned long touch_timer = 0;
unsigned long wifi_reconnect_timer = 0;
unsigned long wifi_reconnect_counter = 0;
unsigned long mqtt_reconnect_counter = 0;

String MQTTDEVICEID = "ESP32_FTMS_";

uint8_t mac_addr[6];
Preferences prefs;
esp_reset_reason_t rr;

// treadmill stats
treadmill * configTreadmill;

volatile float incline_interval  = 0.5;
volatile float speed_interval = 0.5;

volatile unsigned long startTime = 0;     // start of revolution in microseconds
volatile unsigned long longpauseTime = 0; // revolution time with no reed-switch interrupt
volatile long accumulatorInterval = 0;    // time sum between display during intervals
volatile unsigned int revCount = 0;       // number of revolutions since last display update
volatile long accumulator4 = 0;           // sum of last 4 rpm times over 4 seconds
volatile long workoutDistance = 0; // FIXME: vs. total_dist ... select either reed/ir/manual

// ttgo tft: 33, 25, 26, 27
// the number of the pushbutton pins
#ifdef BUTTON_1
Button2 btn1(BUTTON_1);
#endif
#ifdef BUTTON_2
Button2 btn2(BUTTON_2);
#endif
#ifdef BUTTON_3
Button2 btn3(BUTTON_3);
#endif


uint16_t    inst_speed;
uint16_t    inst_incline;
uint16_t    inst_grade;
uint8_t     inst_cadence = 1;                 /* Instantaneous Cadence. */
uint16_t    inst_stride_length = 1;           /* Instantaneous Stride Length. */
uint16_t    inst_elevation_gain = 0;
double      total_distance = 0; // m
double      elevation_gain = 0; // m
double      elevation;

float rpm = 0;

float kmph; // kilometer per hour
float kmph_sense;
float mps;  // meter per second
double angle = 0;
double grade_deg = 0;
float incline = 0;

bool bleClientConnected = false;
bool bleClientConnectedPrev = false;

#if LGFX_USE_V1
LGFX tft;
LGFX_Sprite sprite(&tft);
#elif USE_TFT_ESPI
TFT_eSPI tft = TFT_eSPI();
#endif


#ifndef NO_VL53L0X
VL53L0X sensor;
const unsigned long MAX_DISTANCE = 1000;  // Maximum distance in mm
#endif

bool isWifiAvailable = false;
bool isMqttAvailable = false;

static TwoWire I2C_0 = TwoWire(0);

#ifndef NO_MPU6050
static MPU6050 mpu(I2C_0);
#endif

static GPIOExtenderAW9523 GPIOExtender(I2C_0);

#ifdef MQTT_USE_SSL
WiFiClientSecure espClient;
#else
WiFiClient espClient;
#endif
#ifdef ASYNC_TCP_SSL_ENABLED
AsyncWebServer server(443);
#else
AsyncWebServer server(80);
#endif
AsyncWebSocket ws("/ws");
PubSubClient client(espClient);  // mqtt client

// note: Fitness Machine Feature is a mandatory characteristic (property_read)
#define FTMSService BLEUUID((uint16_t)0x1826)

BLEServer* pServer = NULL;
BLECharacteristic* pTreadmill    = NULL;
BLECharacteristic* pFeature      = NULL;
BLECharacteristic* pControlPoint = NULL;
BLECharacteristic* pStatus       = NULL;
BLEAdvertisementData advert;
BLEAdvertisementData scan_response;
BLEAdvertising *pAdvertising;

// {0x2ACD,"Treadmill Data"},
BLECharacteristic TreadmillDataCharacteristics(BLEUUID((uint16_t)0x2ACD),
#ifndef NIMBLE
					       BLECharacteristic::PROPERTY_NOTIFY
#else
					       NIMBLE_PROPERTY::NOTIFY
#endif
					       );

BLECharacteristic FitnessMachineFeatureCharacteristic(BLEUUID((uint16_t)0x2ACC),
#ifndef NIMBLE
						      BLECharacteristic::PROPERTY_READ
#else
						      NIMBLE_PROPERTY::READ
#endif
						      );

BLEDescriptor TreadmillDescriptor(BLEUUID((uint16_t)0x2901)
#ifdef NIMBLE
				  , NIMBLE_PROPERTY::READ,1000
#endif
				  );

// seems kind of a standard callback function
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleClientConnected = true;
  };

  void onDisconnect(BLEServer* pServer) {
    bleClientConnected = false;
  }
};

void logText(const char *text)
{
  // Serial consol
  DEBUG_PRINTF(text);
  gfxLogText(text);

}

void logText(String text)
{
  logText(text.c_str());
}

void delayWithDisplayUpdate(unsigned long delayMilli)
{
  unsigned long timeStartMilli = millis();
  unsigned long currentMilli = timeStartMilli;
  while((currentMilli - timeStartMilli) < delayMilli)
  {
    gfxUpdateLoopHandler();
    unsigned long timeLeftMilli = delayMilli - (currentMilli - timeStartMilli) ; 
    if (timeLeftMilli >= 5 )
    {
      delay(5);
    }
    else
    {
      delay(timeLeftMilli);
    }
    currentMilli = millis();
  }
}



void initBLE() {
  logText("initBLE\n");

  BLEDevice::init(MQTTDEVICEID.c_str());  // set server name (here: MQTTDEVICEID)
  // create BLE Server, set callback for connect/disconnect
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // create the fitness machine service within the server
  BLEService *pService = pServer->createService(FTMSService);

  pService->addCharacteristic(&TreadmillDataCharacteristics);
  TreadmillDescriptor.setValue("Treadmill Descriptor Value ABC");
  TreadmillDataCharacteristics.addDescriptor(&TreadmillDescriptor);
  pService->addCharacteristic(&FitnessMachineFeatureCharacteristic);

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
#ifndef NIMBLE
  TreadmillDataCharacteristics.addDescriptor(new BLE2902());
  FitnessMachineFeatureCharacteristic.addDescriptor(new BLE2902());
  //pFeature->addDescriptor(new BLE2902());
#else
  /***************************************************
   NOTE: DO NOT create a 2902 descriptor.
   it will be created automatically if notifications
   or indications are enabled on a characteristic.
  ****************************************************/
#endif
  // start service
  pService->start();
  pAdvertising = pServer->getAdvertising();
  pAdvertising->setScanResponse(true);
  pAdvertising->addServiceUUID(FTMSService);
  //pAdvertising->setMinPreferred(0x06);  // set value to 0x00 to not advertise this parameter

  pAdvertising->start();
#if 0
  delay(2000); // added to keep tft msg a bit longer ...
#endif
}


void initSPIFFS() {
  logText("initSPIFFS\n");

  if (!SPIFFS.begin(true)) { // true = formatOnFail
    logText("Cannot mount SPIFFS volume FAILED!!!\n");
  }
  else {
    DEBUG_PRINTLN("SPIFFS Setup Done");
  }

  logText("initSPIFFS Done!\n");
}


void doReset()
{
  setTime(0,0,0,0,0,0);
  total_distance = 0;
  elevation_gain = 0;
  kmph = 0.5;
  incline = 0;

  // calibrate
}

bool logEvent(EventType event)
{

  switch(event)
  {
    case EventType::KEY_UP:               logText("[Event] KEY_UP\n"); break;
    case EventType::KEY_LEFT:             logText("[Event] KEY_LEFT\n"); break;
    case EventType::KEY_DOWN:             logText("[Event] KEY_DOWN\n"); break;
    case EventType::KEY_RIGHT:            logText("[Event] KEY_RIGHT\n"); break;
    case EventType::KEY_OK:               logText("[Event] KEY_OK\n"); break;
    case EventType::KEY_BACK:             logText("[Event] KEY_BACK\n"); break;
    case EventType::TREADMILL_REED:       logText("[Event] TREADMILL_REED\n"); break;
    case EventType::TREADMILL_START:      logText("[Event] TREADMILL_START\n"); break;
    case EventType::TREADMILL_SPEED_DOWN: logText("[Event] TREADMILL_SPEED_DOWN\n"); break;
    case EventType::TREADMILL_INC_DOWN:   logText("[Event] TREADMILL_INC_DOWN\n"); break;
    case EventType::TREADMILL_STOP:       logText("[Event] TREADMILL_STOP\n"); break;
    case EventType::TREADMILL_SPEED_UP:   logText("[Event] TREADMILL_SPEED_UP\n"); break;
    case EventType::TREADMILL_INC_UP:     logText("[Event] TREADMILL_INC_UP\n"); break;
  }
  return false;
}

bool menuEvent(EventType event)
{

  switch(event)
  {
    case EventType::KEY_OK:               gfxShowScreenMain(); return true;
    case EventType::KEY_BACK:             gfxShowScreenBoot(); return true;
  }
  return false;
}

// A simple event handler, currenly just a call stack, an event queue would be smarter
// but this solves the problem as a start, not sure it we really have the usecase where
// we need a queue, lets add it in that case

void handle_event(EventType event)
{
  // Add more event handlers here in prio order, checking if handled before running
  // each handle will return true is event is handled so we can stop the event check
  // in that case.
  // Currently there is no queue so if Events are translated to new events and call
  // handle_event() to post them take care to not get into loops.
  if (logEvent(event)) return;
  if (menuEvent(event)) return;
  if (GPIOExtender.pressEvent(event)) return;

  DEBUG_PRINTF("handle_event() Cant handle Event:0x%x\n",static_cast<uint32_t>(event));
  return;
}

void initButton()
{
#ifdef BUTTON_1
  // button 1 (GPIO 0) control auto/manual mode and reset timers
  btn1.setTapHandler([](Button2 & b) {
    unsigned int time = b.wasPressedFor();
    DEBUG_PRINTLN("Button 1 TapHandler");
    if (time > 3000) { // > 3sec enters config menu
      DEBUG_PRINTLN("RESET timer/counter!");
      doReset();
    }
    else if (time > 600) {
      DEBUG_PRINTLN("Button 1 long click...");
      // reset to manual mode
      speedInclineMode = MANUAL;
      DEBUG_PRINT("speedInclineMode=");
      DEBUG_PRINTLN(speedInclineMode);
      gfxUpdateHeader();
    }
    else { // button1 short click toggle speed/incline mode
      DEBUG_PRINTLN("Button 1 short click...");
      speedInclineMode += 1;
      speedInclineMode %= _NUM_MODES_;
      DEBUG_PRINT("speedInclineMode=");
      DEBUG_PRINTLN(speedInclineMode);
      gfxUpdateHeader();
    }

  });
#endif

  // if only two buttons, then button 1 switches mode and button 2 short is up, button 2 long is down
  // if three buttons use button 2 short as up and button 3 short as down
  // fixme: keep button2 long handler for now does not hurt
  // initially for testing purpose ... but if we can access the controller while running it can serve as backup
  // if sensors fail or suddnely send incorrect readings (yeah that happend to me) ... but since the controller
  // might not be accessable we have the web interface that can run on the smartphone
#ifdef BUTTON_2
  // short  click = up
  // longer click = down
  btn2.setTapHandler([](Button2& b) {
    unsigned int time = b.wasPressedFor();
    DEBUG_PRINTLN("Button 2 TapHandler");
    if (time > 3000) { // > 3sec enters config menu
      //DEBUG_PRINTLN("very long (>3s) click ... do nothing");
    }
    else if (time > 500) {
      DEBUG_PRINTLN("long (>500ms) click...");
      if ((speedInclineMode & SPEED) == 0)
	      speedDown();
      if ((speedInclineMode & INCLINE) == 0)
	inclineDown();
    }
    else {
      DEBUG_PRINTLN("short click...");
      if ((speedInclineMode & SPEED) == 0)
	speedUp();
      if ((speedInclineMode & INCLINE) == 0)
	inclineUp();
    }
  });
#endif

#ifdef BUTTON_3
  // short  click = down
  btn3.setTapHandler([](Button2& b) {
    unsigned int time = b.wasPressedFor();
    DEBUG_PRINTLN("Button 3 TapHandler");
    if (time > 3000) {
      // DEBUG_PRINTLN("very long (>3s) click ... do nothing");
    }
    else {
      DEBUG_PRINTLN("short click...");
      if ((speedInclineMode & SPEED) == 0)
        speedDown();
      if ((speedInclineMode & INCLINE) == 0)
        inclineDown();
    }
  });
#endif
}

void loop_handle_button()
{
#ifdef BUTTON_1
  btn1.loop();
#endif
#ifdef BUTTON_2
  btn2.loop();
#endif
#ifdef BUTTON_3
    btn3.loop();
#endif
}

void loop_handle_touch() {
#if defined (HAS_TOUCH_DISPLAY)
#endif
}

void IRAM_ATTR reedSwitch_ISR()
{
  // calculate the microseconds since the last interrupt.
  // Interupt activates on falling edge.
  uint32_t usNow = micros();
  uint32_t test_elapsed = usNow - startTime;

  // handle button bounce (ignore if less than 300 microseconds since last interupt)
  if (test_elapsed < 300) {
    return;
  }

  //if (test_elapsed > 1000000) {
  if (test_elapsed > 1000000) {  // assume the treadmill isn't spinning in 1 second.
    startTime = usNow; // reset the clock;
    longpauseTime = 0;
    return;
  }
  if (test_elapsed > longpauseTime / 2) {
    // acts as a debounce, don't looking for interupts soon after the first hit.
    //Serial.println(test_elapsed); //Serial.println(" Counted");

    startTime = usNow;  // reset the clock
    //long elapsed = test_elapsed;
    longpauseTime = test_elapsed;

    revCount++;
    workoutDistance += configTreadmill->belt_distance;
    accumulatorInterval += test_elapsed;
  }
}

// two IR-Sensors
void IRAM_ATTR speedSensor1_ISR() {
  if (! t1_valid) {
    t1 = micros();
    t1_valid = true;
  }
}

void IRAM_ATTR speedSensor2_ISR() {
  if (t1_valid && !t2_valid) {
    t2 = micros();
    t2_valid = true;
  }
}

// UI controlled (web or button on esp32) speedUp
void speedUp()
{
  if (speedInclineMode & SPEED) {
    handle_event(EventType::TREADMILL_SPEED_UP);
    return;
  }

  kmph += speed_interval;
  if (kmph > configTreadmill->max_speed) kmph = configTreadmill->max_speed;
  DEBUG_PRINT("speed_up, new speed: ");
  DEBUG_PRINTLN(kmph);
}

// UI controlled (web or button on esp32) speedDown
void speedDown()
{
  if (speedInclineMode & SPEED) {
    handle_event(EventType::TREADMILL_SPEED_DOWN);
    return;
  }

  kmph -= speed_interval;
  if (kmph < configTreadmill->min_speed) kmph = configTreadmill->min_speed;
  DEBUG_PRINT("speed_down, new speed: ");
  DEBUG_PRINTLN(kmph);
}

// UI controlled (web or button on esp32) inclineUp
void inclineUp()
{
  if (speedInclineMode & INCLINE) {
    handle_event(EventType::TREADMILL_INC_UP);
    return;
  }

  incline += incline_interval; // incline in %
  if (incline > configTreadmill->max_incline) incline = configTreadmill->max_incline;
  angle = atan2(incline, 100);
  grade_deg = angle * 57.296;
  DEBUG_PRINT("incline_up, new incline: ");
  DEBUG_PRINTLN(incline);
}

// UI controlled (web or button on esp32) inclineDown
void inclineDown()
{
  if (speedInclineMode & INCLINE) {
    handle_event(EventType::TREADMILL_INC_DOWN);
    return;
  }

  incline -= incline_interval;
  if (incline <= configTreadmill->min_incline) incline = configTreadmill->min_incline;
  angle = atan2(incline, 100);
  grade_deg = angle * 57.296;
  DEBUG_PRINT("incline_down, new incline: ");
  DEBUG_PRINTLN(incline);
}

// angleSensorTreadmillConversion()
// If a treadmill has some special placement of the angle sensor
// here is where that value is converted from sensor read to proper angle of running area.

double angleSensorTreadmillConversion(double inAngle) {
  double convertedAngle = inAngle;
#if TREADMILL_MODEL == NORDICTRACK_12SI
  // TODO: Maybe this should be a config somewhere together with sensor orientation

  /* If Sensor is placed in inside the treadmill engine
    TODO: Maybe this can be automatic, e.g. Let user selct a incline at a time
          and record values and select inbeween bounderies.
          If treadmill support stearing the incline maybe it can also be an automatic
          calibration step. e.g. move it max down, callibrate sensor, step up max and measure
          User input treadmill "Max incline" value and Running are length somehow.
              /|--- ___
           c / |        --- ___ a
            /  |x              --- ___  Running area
           /   |_                      --- ___
          / A  | |                           C ---  ___

    A = inAngle (but we want the angle C)
    sin(A)=x/c     sin(C)=x/a
    x=c*sin(A)     x=a*sin(C)
    C=asin(c*sin(A)/a)
  */
  double c = 32.0;  // lenght of motor part in cm
  double a = 150.0; // lenght of running area in cm
  convertedAngle = asin(c*sin(inAngle * DEG_TO_RAD)/a) * RAD_TO_DEG;
#endif
  return convertedAngle;
}

// getIncline()
// This will read the used "incline" sensor, run this periodically
// The following global variables will be updated
//    angle - the angle of the running are
//    incline - the incline value (% of angle between 0 and 45 degree)
//              incline is usally the value shown by your treadmill.

float getIncline() {
  double sensorAngle = 0.0;
  if (hasVL53L0X) {
    // calc incline/angle from distance
    // depends on sensor placement ... TODO: configure via webinterface
  }
  else if (hasMPU6050) {
    // MPU6050 returns a incline/grade in degrees(!)
#ifndef NO_MPU6050
    // TODO: configure sensor orientation  via webinterface
    // FIXME: maybe get some rolling-average of Y-angle to smooth things a bit (same for speed)
    // mpu.getAngle[XYZ]
    sensorAngle = mpu.getAngleY();
    angle = angleSensorTreadmillConversion(sensorAngle);

    if (angle < 0) angle = 0;  // TODO We might allow running downhill

    char yStr[5];
    char inclineStr[6];
    snprintf(yStr, 5, "%.2f", angle);

    incline = tan(angle * DEG_TO_RAD) * 100;
    snprintf(inclineStr, 6, "%.1f", incline);

    //client.publish(getTopic(MQTT_TOPIC_Y_ANGLE), yStr);
    client.publish(getTopic(MQTT_TOPIC_INCLINE), inclineStr);

#else
    //incline = 0;
    //angle = 0;
#endif
  }

  if (incline <= configTreadmill->min_incline) incline = configTreadmill->min_incline;
  if (incline > configTreadmill->max_incline)  incline = configTreadmill->max_incline;

  //DEBUG_PRINTF("sensor angle (%.2f): used angle: %.2f: -> incline: %f%%\n",sensorAngle, angle, incline);

  // probably need some more smoothing here ...
  // ...
  return incline;
}


void setSpeed(float speed)
{
  kmph = speed;
  if (speed > configTreadmill->max_speed) kmph = configTreadmill->max_speed;
  if (speed < configTreadmill->min_speed) kmph = configTreadmill->min_speed;

  DEBUG_PRINT("setSpeed: ");
  DEBUG_PRINTLN(kmph);
}

void setSpeedInterval(float interval)
{
  DEBUG_PRINT("set_speed_interval: ");
  DEBUG_PRINTLN(interval);

  if ((interval < 0.1) || (interval > 2.0)) {
    DEBUG_PRINTLN("INVALID SPEED INTERVAL");
  }

  speed_interval = interval;
}

String readSpeed()
{
  return String(kmph);
}

String readDist()
{
  return String(total_distance / 1000);
}

String readIncline()
{
  return String(incline);
}

String readElevation()
{
  return String(elevation_gain);
}

String readHour() {
  return String(hour());
}

String readMinute() {
  int m = minute();
  String mStr(m);

  if (m < 10)
    mStr = "0" + mStr;

  return mStr;
}

String readSecond() {
  int s = second();
  String sStr(s);

  if (s < 10)
    sStr = "0" + sStr;

  return sStr;
}

// void calculateRPM() {
//   // divide number of microseconds in a minute, by the average interval.
//   if (revCount > 0) { // confirm there was at least one spin in the last second
//     hasReed = true;
//     DEBUG_PRINT("revCount="); DEBUG_PRINTLN(revCount);
//     // rpmaccumulatorInterval = 60000000/(accumulatorInterval/revCount);
//     rpm = 60000000 / (accumulatorInterval / revCount);
//     //accumulatorInterval = 0;
//     //Test = Calculate average from last 4 rpms - response too slow
//     accumulator4 -= (accumulator4 >> 2);
//     accumulator4 += rpm;
//     mps = belt_distance * (rpm) / (60 * 1000);
//   }
//   else {
//     rpm = 0;
//     //rpmaccumulatorInterval = 0;
//     accumulator4 = 0;  // average rpm of last 4 samples
//   }
//   revCount = 0;
//   accumulatorInterval = 0;
//   //mps = belt_distance * (rpm) / (60 * 1000);
// }

String mqtt_topics[] {
  "home/treadmill/%MQTTDEVICEID%/setconfig",
  "home/treadmill/%MQTTDEVICEID%/state",
  "home/treadmill/%MQTTDEVICEID%/version",
  "home/treadmill/%MQTTDEVICEID%/ipaddr",
  "home/treadmill/%MQTTDEVICEID%/rst",
  "home/treadmill/%MQTTDEVICEID%/speed",
  "home/treadmill/%MQTTDEVICEID%/rpm",
  "home/treadmill/%MQTTDEVICEID%/incline",
  "home/treadmill/%MQTTDEVICEID%/y_angle",
  "home/treadmill/%MQTTDEVICEID%/dist",
  "home/treadmill/%MQTTDEVICEID%/elegain"
};

void setupMqttTopic(const String &id)
{
  for (unsigned i = 0; i < MQTT_NUM_TOPICS; ++i) {
    mqtt_topics[i].replace("%MQTTDEVICEID%", id);
  }
}

const char* getTopic(topics_t topic) {
  return mqtt_topics[topic].c_str();
}

const char* getRstReason(esp_reset_reason_t r) {
  switch(r) {
  case ESP_RST_UNKNOWN:    return "ESP_RST_UNKNOWN";   //!< Reset reason can not be determined
  case ESP_RST_POWERON:    return "ESP_RST_POWERON";   //!< Reset due to power-on event
  case ESP_RST_EXT:        return "ESP_RST_EXT";       //!< Reset by external pin (not applicable for ESP32)
  case ESP_RST_SW:         return "ESP_RST_SW";        //!< Software reset via esp_restart
  case ESP_RST_PANIC:      return "ESP_RST_PANIC";     //!< Software reset due to exception/panic
  case ESP_RST_INT_WDT:    return "ESP_RST_INT_WDT";   //!< Reset (software or hardware) due to interrupt watchdog
  case ESP_RST_TASK_WDT:   return "ESP_RST_TASK_WDT";  //!< Reset due to task watchdog
  case ESP_RST_WDT:        return "ESP_RST_WDT";       //!< Reset due to other watchdogs
  case ESP_RST_DEEPSLEEP:  return "ESP_RST_DEEPSLEEP"; //!< Reset after exiting deep sleep mode
  case ESP_RST_BROWNOUT:   return "ESP_RST_BROWNOUT";  //!< Brownout reset (software or hardware)
  case ESP_RST_SDIO:       return "ESP_RST_SDIO";      //!< Reset over SDIO
  }
  return "INVALID";
}

void showInfo() {
  String intoText = String("ESP32 FTMS - ") + VERSION + String("\n");
  logText(intoText.c_str());
  std::string intoText2 = configTreadmill->getName();
  logText(intoText2.c_str());
  intoText = String("\nSpeed[") + configTreadmill->min_speed + String(", ") + configTreadmill->max_speed + String("]\n");
  logText(intoText.c_str());
  intoText = String("Incline[") + configTreadmill->min_incline + String(", ") + configTreadmill->max_incline + String("]\n");
  logText(intoText.c_str());
  intoText = String("Dist/REED:") + configTreadmill->belt_distance + String("mm\n");
  logText(intoText.c_str());
  intoText = String("REED:") + hasReed + String("\n");
  logText(intoText.c_str());
  intoText = String("MPU6050:") + hasMPU6050 + String("\n");
  logText(intoText.c_str());
  intoText = String("VL53L0X:") + hasVL53L0X + String("\n");
  logText(intoText.c_str());
  intoText = String("IrSense:") + hasIrSense + String("\n");
  logText(intoText.c_str());
  intoText = String("GPIOExtender(AW9523):") + GPIOExtender.isAvailable() + String("\n");
  logText(intoText.c_str());

}

#ifdef AW9523_IRQ_MODE
static void IRAM_ATTR GPIOExtenderInterrupt(void) {
  GPIOExtender.gotInterrupt();
}
#endif

static void initGPIOExtender(void) {
  while (!GPIOExtender.begin())
  {
    logText("GPIOExtender not found");
    return;
  }

#ifdef AW9523_IRQ_MODE
  pinMode(AW9523_INTERRUPT_PIN, INPUT_PULLUP); 
  //pinMode(AW9523_INTERRUPT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(AW9523_INTERRUPT_PIN), GPIOExtenderInterrupt, CHANGE);
  GPIOExtender.getPins(); // Get pins one to clear intrrupts
#endif
  logText("GPIOExtender Setup Done");
}

void setup() {
  DEBUG_BEGIN(115200);
  DEBUG_PRINTLN("setup started");
  rr = esp_reset_reason();

  // fixme check return code
  esp_efuse_mac_get_default(mac_addr);

  MQTTDEVICEID += String(mac_addr[4], HEX);
  MQTTDEVICEID += String(mac_addr[5], HEX);
  setupMqttTopic(MQTTDEVICEID);

  // Setup treadmill config
#ifndef TREADMILL_MODEL
  #error "***** ATTENTION NO TREADMILL MODEL DEFINED ******"
#elif TREADMILL_MODEL == TAURUS_9_5
  configTreadmill = new treadmillTaurus9_5();
#elif TREADMILL_MODEL == NORDICTRACK_12SI
  configTreadmill = new treadmillNorthtrack12_2_Si();
#else
  #error "Unexpected value for TREADMILL_MODEL defined!"
#endif

  incline_interval  = configTreadmill->incline_interval_min;
  speed_interval = configTreadmill->speed_interval_min;
  // initial min treadmill speed
  kmph = 0.5;
  incline = 0;
  grade_deg = 0;
  angle = 0;
  elevation = 0;
  elevation_gain = 0;

  initButton();

  gfxInit();
  gfxShowScreenBoot();

  logText("Setup Started\n");

#if defined(SPEED_IR_SENSOR1) && defined(SPEED_IR_SENSOR2)
  // pinMode(SPEED_IR_SENSOR1, INPUT_PULLUP); //TODO used?
  // pinMode(SPEED_IR_SENSOR1, INPUT_PULLUP); //TODO used?
  attachInterrupt(SPEED_IR_SENSOR1, speedSensor1_ISR, FALLING); //TODO used?
  attachInterrupt(SPEED_IR_SENSOR2, speedSensor2_ISR, FALLING); //TODO used?
#endif

  // note: I have 10k pull-up on SPEED_REED_SWITCH_PIN
  pinMode(SPEED_REED_SWITCH_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(SPEED_REED_SWITCH_PIN), reedSwitch_ISR, FALLING);

  I2C_0.begin(SDA_0 , SCL_0 , I2C_FREQ);

  initGPIOExtender();

  initBLE();
  initSPIFFS();

  isWifiAvailable = setupWifi() ? false : true;
  gfxUpdateHeader();

  if (isWifiAvailable) {
    DEBUG_PRINTLN("Init Webserver");
    initAsyncWebserver();
    initWebSocket();
  }
  //else show offline msg, halt or reboot?!

  if (isWifiAvailable) {
    isMqttAvailable = mqttConnect(true);
    delayWithDisplayUpdate(2000);
  }
  gfxUpdateHeader();

  logText("mqtt setup Done!\n");

#ifndef NO_MPU6050
  logText("init MPU6050\n");

  byte status = mpu.begin();
  DEBUG_PRINT("MPU6050 status: ");
  DEBUG_PRINTLN(status);
  if (status != 0) {
    logText("MPU6050 setup failed!\n");
  }
  else {
    logText("Calc offsets, do not move MPU6050 (3sec)\n");

    mpu.calcOffsets(); // gyro and accel.
    delayWithDisplayUpdate(2000);
    speedInclineMode |= INCLINE;
    hasMPU6050 = true;

    logText("MPU6050 OK!\n");
  }
#else
  hasMPU6050 = false;
#endif
#ifndef NO_VL53L0X
  logText("init VL53L0X\n");


  sensor.setTimeout(500);
  if (!sensor.init()) {
    DEBUG_PRINTLN("Failed to detect and initialize VL53L0X sensor!");

    logText("VL53L0X setup failed!\n");
  }
  else {
    logText("VL53L0X initialized!\n");
    hasVL53L0X = true;
  }
#else
  hasVL53L0X = false;
#endif

  logText("--- Setup done ---\n");
  showInfo();

  gfxShowScreenMain();
  gfxUpdateHeader();

  setTime(0,0,0,0,0,0);
  setupDone = true;
}

void loop_handle_WIFI() {
  // re-connect to wifi
  if ((WiFi.status() != WL_CONNECTED) && ((millis() - wifi_reconnect_timer) > WIFI_CHECK)) {
    wifi_reconnect_timer = millis();
    isWifiAvailable = false;
    isMqttAvailable = false;
    mqtt_reconnect_counter = 0;

    DEBUG_PRINTLN("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();
  }
  if (!isWifiAvailable && (WiFi.status() == WL_CONNECTED)) {
    // connection was lost and now got reconnected ...
    isWifiAvailable = true;
    wifi_reconnect_counter++;
    gfxUpdateWIFI(wifi_reconnect_counter, getWifiIpAddr());
  }
  if (!isMqttAvailable && isWifiAvailable)
  {
    // TODO Add a menu item to start a new retry?
    //      Or and mqtt enable/disable config when we have on device configs
    //      Do we want to retry this more less often like every 15 min or 1h?
    // Limit this to 2 retrys to not bug down a system forever if not availible
    if (mqtt_reconnect_counter < 2)
    {
      mqtt_reconnect_counter++;
      isMqttAvailable = mqttConnect(true);
    }
  }
}

void loop_handle_BLE() {
  // if changed to connected ...
  if (bleClientConnected && !bleClientConnectedPrev) {
    bleClientConnectedPrev = true;
    DEBUG_PRINTLN("BT Client connected!");
    gfxUpdateBTConnectionStatus(bleClientConnectedPrev);
  }
  else if (!bleClientConnected && bleClientConnectedPrev) {
    bleClientConnectedPrev = false;
    DEBUG_PRINTLN("BT Client disconnected!");
    gfxUpdateBTConnectionStatus(bleClientConnectedPrev);
  }
}

void loop() {
#ifndef NO_MPU6050
  mpu.update();
#endif

  GPIOExtender.loopHandler();
  loop_handle_button();
  loop_handle_touch();
  loop_handle_WIFI();
  loop_handle_BLE();
  gfxUpdateLoopHandler();

  // check ir-speed sensor if not manual mode
  if (t2_valid) { // hasIrSense = true
    hasIrSense = true;
    unsigned long t = t2 - t1;
    unsigned long c = 359712; // d=10cm
    kmph_sense = (float)(1.0 / t) * c;
    noInterrupts();
    t1_valid = t2_valid = false;
    interrupts();
    DEBUG_PRINTF("IrSense: t=%li kmph_sense=%f\n",t,kmph_sense);
  }

  // testing ... every second
  if ((millis() - sw_timer_clock) > EVERY_SECOND) {
    sw_timer_clock = millis();

    if (speedInclineMode & INCLINE) {
      incline = getIncline(); // sets global 'angle' and 'incline' variable
    }

    // total_distance = ... v = d/t -> d = v*t -> use v[m/s]
    if (speedInclineMode & SPEED) {  // get speed from sensor (no-manual mode)
      // FIXME: ... probably can get rid of this if/else if ISR for the ir-sensor
      // and calc rpm from reed switch provide same unit
      if (hasIrSense) {
        kmph = kmph_sense;
        mps = kmph / 3.6; // meter per second (EVERY_SECOND)
        total_distance += mps;
      }
      else if (hasReed) {
        if (revCount > 0) { // confirm there was at least one spin in the last second
          noInterrupts();
          rpm = 60000000 / (accumulatorInterval / revCount);
          revCount = 0;
          accumulatorInterval = 0;
          interrupts();
        }
        else {
          rpm = 0;
          //rpmaccumulatorInterval = 0;
          //accumulator4 = 0;  // average rpm of last 4 samples
        }
        mps = configTreadmill->belt_distance * (rpm) / (60 * 1000); // meter per sec
        kmph = mps * 3.6;                          // km per hour
        total_distance = workoutDistance / 1000;   // conv mm to meter
      }
    }
    else {
      mps = kmph / 3.6;
      total_distance += mps;
    }
    //elevation_gain += (double)(sin(angle) * mps);
    elevation_gain += incline / 100 * mps;

 #if 0
    DEBUG_PRINT("mps = d: ");       DEBUG_PRINT(mps);
    DEBUG_PRINT("   angle: ");      DEBUG_PRINT(angle);
    DEBUG_PRINT("   h (m): ");      DEBUG_PRINT(sin(angle) * mps);
    DEBUG_PRINT("   h gain (m): "); DEBUG_PRINT(elevation_gain);
    DEBUG_PRINT("   kmph: ");       DEBUG_PRINT(kmph);
    DEBUG_PRINT("   incline: ");    DEBUG_PRINT(incline);
    DEBUG_PRINT("   angle: ");      DEBUG_PRINT(grade_deg);
    DEBUG_PRINT("   dist km: ");    DEBUG_PRINTLN(total_distance/1000);
#endif
    char kmphStr[6];
    snprintf(kmphStr,    6, "%.1f", kmph);

    client.publish(getTopic(MQTT_TOPIC_SPEED),   kmphStr);
    //client.publish(getTopic(MQTT_TOPIC_DIST),    readDist().c_str());
    //client.publish(getTopic(MQTT_TOPIC_ELEGAIN), readElevation().c_str());

    gfxUpdateDisplay();
    notifyClients();

    uint8_t treadmillData[34] = {};
    uint16_t flags = 0x0018;  // b'000000011000
    //                             119876543210
    //                             20
    // get speed and incline ble ready
    inst_speed   = kmph * 100;    // kilometer per hour with a resolution of 0.01
    inst_incline = incline * 10;  // percent with a resolution of 0.1
    inst_grade   = grade_deg * 10;
    inst_elevation_gain = elevation_gain * 10;

    // now data is filled starting at bit0 and appended for the
    // fields we 'enable' via the flags above ...
    //treadmillData[0,1] -> flags
    treadmillData[0] = (uint8_t)(flags & 0xFF);
    treadmillData[1] = (uint8_t)(flags >> 8);

    // speed
    treadmillData[2] = (uint8_t)(inst_speed & 0xFF);
    treadmillData[3] = (uint8_t)(inst_speed >> 8);

    // incline & degree
    treadmillData[4] = (uint8_t)(inst_incline & 0xFF);
    treadmillData[5] = (uint8_t)(inst_incline >> 8);
    treadmillData[6] = (uint8_t)(inst_grade & 0xFF);
    treadmillData[7] = (uint8_t)(inst_grade >> 8);

    // Positive Elevation Gain 16 Meters with a resolution of 0.1
    treadmillData[8] = (uint8_t)(inst_elevation_gain & 0xFF);
    treadmillData[9] = (uint8_t)(inst_elevation_gain >> 8);

    // flags do enable negative elevation as well but this will be always 0

    TreadmillDataCharacteristics.setValue(treadmillData, 34);
    TreadmillDataCharacteristics.notify();
  }
}
