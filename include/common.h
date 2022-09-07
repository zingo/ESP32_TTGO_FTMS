#pragma once

#define DEBUG 1
//#define DEBUG_MQTT 1
#include "debug_print.h"
#include "treadmill.h"
//#include "wifi_mqtt_creds.h"

#include <Arduino.h>

#include <WiFi.h>
#if ASYNC_TCP_SSL_ENABLED
#include <AsyncTCP_SSL.h>
#endif
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#ifdef MQTT_USE_SSL
#include <WiFiClientSecure.h>
extern WiFiClientSecure espClient;
#else
extern WiFiClient espClient;
#endif

extern PubSubClient client;
extern AsyncWebServer server;
extern AsyncWebSocket ws;


#if LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
extern LGFX tft;

#ifdef HAS_TOUCH_DISPLAY
/*
extern LGFX_Button touchButtons[];
extern LGFX_Button btnSpeedToggle;
extern LGFX_Button btnInclineToggle;
extern LGFX_Button btnSpeedUp;
extern LGFX_Button btnSpeedDown;
extern LGFX_Button btnInclineUp;
extern LGFX_Button btnInclineDown;
*/
#endif

#else
#include <TFT_eSPI.h>
extern TFT_eSPI tft;
#endif

// display is configured within platformio.ini
#ifndef TFT_WIDTH
#define TFT_WIDTH  135
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 240
#endif


enum SensorModeFlags {
		      MANUAL  = 0,
		      SPEED   = 1,
		      INCLINE = 2,
		      _NUM_MODES_ = 4
};

extern treadmill * configTreadmill; // TODO save used treadmill in some storage area and add runtime config for it

extern float  kmph;
extern float  incline;
extern double total_distance;
extern double elevation_gain;
extern uint8_t speedInclineMode;

extern boolean setupDone;
extern bool bleClientConnected;
extern unsigned long wifi_reconnect_counter;

extern esp_reset_reason_t rr;

extern String MQTTDEVICEID;
extern const char* VERSION;

extern volatile float speed_interval;

// mqtt topics
enum topics_t {
	       MQTT_TOPIC_SETCONFIG = 0,
	       MQTT_TOPIC_STATE,
	       MQTT_TOPIC_VERSION,
	       MQTT_TOPIC_IPADDR,
	       MQTT_TOPIC_RST,
	       MQTT_TOPIC_SPEED,
	       MQTT_TOPIC_RPM,
	       MQTT_TOPIC_INCLINE,
	       MQTT_TOPIC_Y_ANGLE,
	       MQTT_TOPIC_DIST,
	       MQTT_TOPIC_ELEGAIN,
	       MQTT_NUM_TOPICS
};

extern String mqtt_topics[];

// Events
enum class EventType {
  KEY_UP,
  KEY_LEFT,
  KEY_DOWN,
  KEY_RIGHT,
  KEY_OK,
  KEY_BACK,
  TREADMILL_REED,
  TREADMILL_START,
  TREADMILL_SPEED_DOWN,
  TREADMILL_INC_DOWN,
  TREADMILL_STOP,
  TREADMILL_SPEED_UP,
  TREADMILL_INC_UP,
};

String readHour();
String readMinute();
String readSecond();
String readDist();
String readIncline();
String readElevation();
String readSpeed();
void speedDown();
void speedUp();
void inclineUp();
void inclineDown();

void initAsyncWebserver();
void initWebSocket();
String getWifiIpAddr();

void gfxInit();
void gfxUpdateLoopHandler();
void gfxUpdateDisplay(); // called once per second
void gfxLogText(const char *text);
void gfxUpdateBTConnectionStatus(bool connected);
void gfxUpdateHeader();
void gfxUpdateWIFI(const unsigned long reconnect_counter, const String &ip);
void gfxShowScreenBoot();
void gfxShowScreenMain();

void setSpeedInterval(float interval);
int setupWifi();
bool mqttConnect( bool draw);
void notifyClients();
const char* getTopic(topics_t topic);
const char* getRstReason(esp_reset_reason_t r);
void handle_event(EventType event);
void logText(const char *text);
void logText(String text);
void delayWithDisplayUpdate(unsigned long delayMilli);
