#ifndef _LIBRARY_AND_PIN_
#define _LIBRARY_AND_PIN_

#include <Arduino.h>

#include <Adafruit_NeoPixel.h>
#include <HardwareSerial.h>
#include <SoftwareSerial.h>
#include "DFRobotDFPlayerMini.h"
#include <HAS2_Wifi.h>
#include <SimpleTimer.h>
#include <SecureOTA.h>
#include "secrets.h"

// 별도 블랙박스 라이브러리(HAS1BlackBox, New_HAS1 저장소). broker URI가 비어 있으면 flash에만 남긴다.
// 3호점 수집 PC의 Mosquitto는 익명 허용이라 암호가 비어 있어도 된다. 사용자 이름이 비어 있으면
// 장치 STA MAC(대문자 12자리)을 쓴다. 값을 바꾸려면 secrets.h에서 먼저 정의한다.
#ifndef TAGMACHINE_BLACKBOX_ENABLED
#define TAGMACHINE_BLACKBOX_ENABLED 1
#endif
#ifndef TAGMACHINE_BLACKBOX_BROKER_URI
#define TAGMACHINE_BLACKBOX_BROKER_URI "mqtt://172.30.1.43:1883"
#endif
#ifndef TAGMACHINE_BLACKBOX_MQTT_USERNAME
#define TAGMACHINE_BLACKBOX_MQTT_USERNAME ""
#endif
#ifndef TAGMACHINE_BLACKBOX_MQTT_PASSWORD
#define TAGMACHINE_BLACKBOX_MQTT_PASSWORD ""
#endif
#include "blackbox.h"

#define DFPLAYER_RX_PIN 39
#define DFPLAYER_TX_PIN 33
#define SUB_BEETLE_RX_PIN 19
#define SUB_BEETLE_TX_PIN 23
#define MAIN_BEETLE_RX_PIN 2
#define MAIN_BEETLE_TX_PIN 15

#define MAIN_LINE_NEOPIXEL_PIN       13
#define MAIN_ROUND_NEOPIXEL_PIN      14
#define SUB_LINE_NEOPIXEL_PIN       5
#define SUB_ROUND_NEOPIXEL_PIN      18
#define ONBOARD_NEOPIXEL_PIN    27

#define RELAY_PIN  21
#endif
