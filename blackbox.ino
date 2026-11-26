#include <BlackBoxWifi.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>

// 태그머신 블랙박스: 태그/로그인/개폐 흐름, 서버 상태 전환, Wi-Fi 이벤트, 서버 폴링 지연,
// 메모리를 flash 링에 남기고 게임(game_state=activate) 밖에서만 MQTT로 수집 PC에 올린다.
// 게임 HTTP, OTA 서명/절차, 파티션 표는 바꾸지 않는다. 기록 형식과 한계는 BLACKBOX.md 참고.

extern bool dfPlayerReady; // dfplayer.ino
static bool bb_ota_hold = false;
// spiffs 정리 결과: 0 필요 없음, 2 실패, 3 이미 시도함(다시 안 함).
static uint8_t bb_claim_result = 0;
static const uint32_t BB_CLAIM_MAGIC = 0x42424331;
RTC_NOINIT_ATTR static uint32_t bb_claim_marker;
static uint32_t bb_forced_outputs = 0;
static uint32_t bb_http_count = 0, bb_http_max_ms = 0, bb_http_total_ms = 0, bb_http_slow = 0;

// 게임 중, 태그 진행 중, OTA 중에는 올리지 않는다. 서버 상태를 한 번도 못 받았으면 닫아 둔다.
static void BlackBoxUploadGate()
{
  const char *phase = (const char *)my["game_state"];
  const bool known = phase && phase[0];
  has1blackbox::blackbox.setUploadAllowed(
      !bb_ota_hold && known && strcmp(phase, "activate") != 0 && !loginDone &&
      WiFi.status() == WL_CONNECTED);
}

void BlackBoxBegin()
{
  static char version[16];
  snprintf(version, sizeof(version), "%d", FIRMWARE_VER);
  has1blackbox::Config config{"HAS3_tagmachine", version, TAGMACHINE_BLACKBOX_BROKER_URI,
                              TAGMACHINE_BLACKBOX_ENABLED != 0, TAGMACHINE_BLACKBOX_MQTT_USERNAME,
                              TAGMACHINE_BLACKBOX_MQTT_PASSWORD};
  // 이 장치의 spiffs는 기본 파티션 표의 1.5 MiB다. 앞 128 KiB만 링으로 쓴다.
  config.largePartition = true;
  has1blackbox::blackbox.begin(config);
  has1blackbox::blackbox.log(has1blackbox::EventType::Boot, esp_reset_reason(), FIRMWARE_VER,
                             PARTITION_VER, 0, "device.boot");
  // Mp3_Setup()은 기록 시작 전에 끝난다. 실패했으면 여기서 남긴다.
  if (!dfPlayerReady) BlackBoxTag("tm.mp3.absent", 0, 0, 0, 0);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    has1blackbox::logWifiMetadata(event, info);
  });
}

void BlackBoxLoop()
{
  static uint32_t previous = 0, emitted = 0, gate_at = 0, max_gap = 0, loops = 0;
  static bool seen = false;
  const uint32_t now = millis();
  if (seen)
  {
    const uint32_t gap = now - previous;
    if (gap > max_gap) max_gap = gap;
  }
  else
  {
    seen = true;
    emitted = now;
    gate_at = now - 250;
  }
  previous = now;
  ++loops;

  if (uint32_t(now - gate_at) >= 250)
  {
    gate_at = now;
    BlackBoxUploadGate();
  }
  if (uint32_t(now - emitted) < 60000) return;

  // 60초 요약: loop가 가장 오래 멈춘 시간(HTTP/연출 delay 포함), 서버 폴링, 메모리, 와이파이.
  has1blackbox::blackbox.log(has1blackbox::EventType::Progress, max_gap, loops,
                             bb_forced_outputs, 0, "main.loop");
  has1blackbox::blackbox.log(has1blackbox::EventType::HttpSummary, bb_http_count, bb_http_max_ms,
                             bb_http_total_ms, bb_http_slow, "http.loop");
  has1blackbox::blackbox.log(has1blackbox::EventType::Health, ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                             uxTaskGetStackHighWaterMark(nullptr), "memory.main");
  has1blackbox::blackbox.log(has1blackbox::EventType::WifiSummary, int(WiFi.status()),
                             WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : INT32_MIN, 0, 0, "wifi");
  emitted = now;
  max_gap = 0;
  loops = 0;
  bb_http_count = 0;
  bb_http_max_ms = 0;
  bb_http_total_ms = 0;
  bb_http_slow = 0;
}

// Recovery 유형으로 남긴다: 라이브러리가 이 유형의 최근 8건을 RTC에도 복사해 두므로,
// 패닉/워치독 재부팅 직전의 흐름이 flash에 못 내려갔어도 다음 부팅에서 다시 기록된다.
void BlackBoxTag(const char *label, int32_t a, int32_t b, int32_t c, int32_t d)
{
  has1blackbox::blackbox.log(has1blackbox::EventType::Recovery, a, b, c, d, label);
}

void BlackBoxState(int32_t kind, const char *state)
{
  has1blackbox::blackbox.log(has1blackbox::EventType::State, kind, 0, 0, 0,
                             state && state[0] ? state : "unknown");
  // 게임이 시작되면 다음 250ms 주기를 기다리지 않고 바로 업로드를 닫는다.
  if (kind == 0) BlackBoxUploadGate();
}

void BlackBoxHttp(uint32_t startedMs)
{
  const uint32_t took = millis() - startedMs;
  ++bb_http_count;
  bb_http_total_ms += took;
  if (took > bb_http_max_ms) bb_http_max_ms = took;
  if (took < 3000) return;
  // 정상 폴링은 수십 ms다. 3초 이상은 서버 무응답/재접속으로 loop가 멈춘 것이다.
  ++bb_http_slow;
  BlackBoxTag("tm.http.slow", (int32_t)took, int(WiFi.status()),
              WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : INT32_MIN, 0);
}

// GitHub OTA는 TLS 다운로드와 flash 쓰기를 한다. MQTT 세션이 메모리를 돌려줄 때까지
// (최대 15초) 기다리고, 블랙박스의 flash 쓰기를 멈춘 상태에서 ota.check()를 돌린다.
// 업데이트가 설치되면 ota.check()가 재부팅하므로 돌아오지 않는다.
void BlackBoxOtaCheck()
{
  bb_ota_hold = true;
  has1blackbox::blackbox.setUploadAllowed(false);
  const uint32_t started = millis();
  // 부팅 직후라면 링 탐색이 끝날 때까지(최대 5초) 기다린다. 탐색 도중에 멈추면 그 부팅 내내 기록이 꺼진다.
  while (TAGMACHINE_BLACKBOX_ENABLED != 0 && !has1blackbox::blackbox.ready() &&
         has1blackbox::blackbox.diagnostics().ringStatus == 0 && uint32_t(millis() - started) < 5000)
    delay(10);
  while (!has1blackbox::blackbox.uploadIdle() && uint32_t(millis() - started) < 15000) delay(10);
  const uint32_t ticket = has1blackbox::blackbox.log(
      has1blackbox::EventType::Recovery, ESP.getFreeHeap(),
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), has1blackbox::blackbox.uploadIdle(),
      (int32_t)(millis() - started), "ota.github.begin");
  if (ticket) has1blackbox::blackbox.flushThrough(ticket, 100);
  has1blackbox::blackbox.setPaused(true);
  // 이미 시작된 flash 동작 하나가 끝나기를 기다린 뒤 OTA를 시작한다.
  BlackBoxQuietEnd(BlackBoxQuietBegin(300));
  ota.check();
  has1blackbox::blackbox.setPaused(false);
  bb_ota_hold = false;
  BlackBoxTag("ota.github.returned", ESP.getFreeHeap(), (int32_t)(millis() - started), 0, 0);
}

// 텔넷 콘솔은 긴 줄(약 64바이트 초과)을 내보내지 못한다. 한 줄을 짧게 나눠 찍는다.
void BlackBoxDiagnosticsPrint()
{
  const has1blackbox::Diagnostics d = has1blackbox::blackbox.diagnostics();
  char line[60];
  snprintf(line, sizeof(line), "[bb] fw=%d heap=%lu min=%lu block=%lu", FIRMWARE_VER,
           static_cast<unsigned long>(ESP.getFreeHeap()),
           static_cast<unsigned long>(ESP.getMinFreeHeap()),
           static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  DebugSerial.println(line);
  snprintf(line, sizeof(line), "[bb] ready=%u status=%u part=%lu claim=%u", d.ready ? 1u : 0u,
           static_cast<unsigned>(d.ringStatus), static_cast<unsigned long>(d.partitionSize),
           static_cast<unsigned>(bb_claim_result));
  DebugSerial.println(line);
  snprintf(line, sizeof(line), "[bb] flash_err=%lu up=%lu up_fail=%lu drop=%lu",
           static_cast<unsigned long>(d.flashErrors), static_cast<unsigned long>(d.uploaded),
           static_cast<unsigned long>(d.uploadFailures), static_cast<unsigned long>(d.dropped));
  DebugSerial.println(line);
  snprintf(line, sizeof(line), "[bb] accepted=%lu persisted=%lu forced=%lu",
           static_cast<unsigned long>(d.accepted), static_cast<unsigned long>(d.persisted),
           static_cast<unsigned long>(bb_forced_outputs));
  DebugSerial.println(line);
}

// 이 펌웨어는 spiffs 파티션을 쓰지 않는다(설정은 NVS). 그런데 예전에 다른 파티션 표로 구운
// 적이 있는 장치는 그 자리에 옛 앱 이미지 조각 같은 내용이 남아 있어, 라이브러리가 "모르는
// 내용"(status 5)으로 보고 기록을 끈다. 그 경우에만 파티션을 한 번 지우고 재시작한다.
// 유효한 링이 있으면 status가 5가 아니므로 기록을 지우는 일은 없다. RTC 표식으로 전원이
// 들어와 있는 동안 한 번만 시도해 재시작이 반복되지 않게 한다.
void BlackBoxClaimSpiffs()
{
  if (TAGMACHINE_BLACKBOX_ENABLED == 0) return;
  const uint32_t started = millis();
  while (has1blackbox::blackbox.diagnostics().ringStatus == 0 &&
         uint32_t(millis() - started) < 5000)
    delay(10);
  const uint8_t status = has1blackbox::blackbox.diagnostics().ringStatus;
  if (status == 1) bb_claim_marker = 0;
  if (status != 5) return;
  if (bb_claim_marker == BB_CLAIM_MAGIC)
  {
    bb_claim_result = 3; // 이미 한 번 지웠는데도 그대로다. 다시 하지 않는다.
    return;
  }
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (!partition || partition->size % 4096 != 0)
  {
    bb_claim_result = 2;
    return;
  }
  // writer의 재시도(ring.begin)와 겹치지 않게 같은 가드를 잡고 지운다.
  if (!has1blackbox::blackbox.tryBeginLedOutput())
  {
    bb_claim_result = 2;
    return;
  }
  DebugSerial.println("[bb] spiffs holds leftovers; erasing once");
  bool ok = true;
  for (size_t offset = 0; ok && offset < partition->size; offset += 4096)
  {
    ok = esp_partition_erase_range(partition, offset, 4096) == ESP_OK;
    delay(1); // 다른 태스크와 워치독에 양보
  }
  has1blackbox::blackbox.endLedOutput();
  if (!ok)
  {
    bb_claim_result = 2;
    DebugSerial.println("[bb] spiffs erase failed");
    return;
  }
  bb_claim_marker = BB_CLAIM_MAGIC;
  DebugSerial.println("[bb] spiffs erased; restarting");
  delay(200);
  ESP.restart();
}

//**************************************** 출력 가드 ****************************************
// flash 쓰기는 캐시를 잠깐 꺼서 진행 중인 네오픽셀 전송이나 소프트웨어 시리얼 비트를 깰 수 있다.
// 블랙박스 writer와 같은 가드를 잡고 출력한다. writer는 가드를 대기 없이 잡고 동작 하나마다
// 놓으므로 보통 바로 잡힌다. waitMs 안에 못 잡으면(섹터 지우는 중 등) 가드 없이 출력하고
// 횟수만 센다: 드문 프레임 깨짐보다 게임 연출이 밀리는 쪽이 더 나쁘다.
bool BlackBoxQuietBegin(uint32_t waitMs)
{
  const uint32_t started = millis();
  for (;;)
  {
    if (has1blackbox::blackbox.tryBeginLedOutput()) return true;
    if (uint32_t(millis() - started) >= waitMs) break;
    delay(1);
  }
  ++bb_forced_outputs;
  return false;
}

void BlackBoxQuietEnd(bool held)
{
  if (held) has1blackbox::blackbox.endLedOutput();
}

void NeoShow(Adafruit_NeoPixel &neo)
{
  const bool held = BlackBoxQuietBegin(30);
  neo.show();
  BlackBoxQuietEnd(held);
}
