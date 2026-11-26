#pragma once
#include <stddef.h>
#include <stdint.h>
#include <Adafruit_NeoPixel.h>
#include <HAS1BlackBox.h>

// 태그머신 블랙박스(HAS1BlackBox) 연결부 - 구현은 blackbox.ino, 기록 형식은 BLACKBOX.md.
// 기본 인자를 쓰지 않는다: Arduino가 .ino 함수의 프로토타입을 자동 생성하므로
// 선언마다 인자 목록이 같아야 한다.
void BlackBoxBegin();
void BlackBoxLoop();
// 태그/로그인/개폐 흐름 기록. label은 27자 이하 ASCII 리터럴.
void BlackBoxTag(const char *label, int32_t a, int32_t b, int32_t c, int32_t d);
// 서버 상태 전환 기록. kind 0 = game_state, 1 = device_state.
void BlackBoxState(int32_t kind, const char *state);
// 서버 폴링(has2wifi.Loop) 한 번이 걸린 시간을 분당 요약에 더한다.
void BlackBoxHttp(uint32_t startedMs);
// GitHub OTA: 업로드를 닫고 flash 기록을 멈춘 뒤 ota.check()를 돌린다.
void BlackBoxOtaCheck();
int BlackBoxDiagnosticsText(char *out, size_t cap);

// 블랙박스 flash 쓰기와 겹치면 깨지는 출력(네오픽셀, DFPlayer 소프트웨어 시리얼)용 가드.
// waitMs 동안 기다려도 못 잡으면 false를 돌려주고, 호출자는 그대로 출력한다.
bool BlackBoxQuietBegin(uint32_t waitMs);
void BlackBoxQuietEnd(bool held);
void NeoShow(Adafruit_NeoPixel &neo);
