# 태그머신 블랙박스

3호점 태그머신(`updated_tagmachine_main`, `third_store`)이 태그·로그인·개폐 흐름, 서버 상태 전환,
Wi-Fi 이벤트, 서버 폴링 지연, 메모리를 `HAS1BlackBox` 라이브러리(New_HAS1 저장소
`libraries/HAS1BlackBox`)의 flash 링에 남기고, 게임 밖에서 MQTT로 수집 PC
(`tools/windows-blackbox-logger`)에 올린다. 게임 HTTP(`has2wifi`), OTA 서명·절차, 파티션 표는
바꾸지 않는다. 라이브러리의 동작과 한계는 라이브러리 README를 따른다.

## 빌드

라이브러리는 스케치북에 설치하지 않아도 된다. New_HAS1을 받아 두고 경로를 넘긴다.

```
arduino-cli compile -b esp32:esp32:ttgo-t1:PartitionScheme=default \
  --library <New_HAS1>/libraries/HAS1BlackBox .
```

`HAS3_tagmachine` 장치 토큰과 `Config::largePartition`을 아는 라이브러리 버전이어야 한다.

## 설정

`Library_and_pin.h`의 기본값. 바꾸려면 `secrets.h`에서 먼저 정의한다.

| 매크로 | 기본값 | 뜻 |
|---|---|---|
| `TAGMACHINE_BLACKBOX_ENABLED` | `1` | `0`이면 기록/업로드 태스크를 만들지 않는다 |
| `TAGMACHINE_BLACKBOX_BROKER_URI` | `mqtt://172.30.1.43:1883` | 비우면 flash에만 남긴다 |
| `TAGMACHINE_BLACKBOX_MQTT_USERNAME` | `""` | 비어 있으면 장치 STA MAC(대문자 12자리) |
| `TAGMACHINE_BLACKBOX_MQTT_PASSWORD` | `""` | 3호점 브로커는 익명 허용이라 비워 둔다 |

파티션은 기존 기본 표(1.5 MiB `spiffs`, `coredump` 없음)를 그대로 쓴다. 링은 `spiffs`의 앞
128 KiB만 차지한다(`largePartition`). 처음 켤 때 `spiffs` 전체가 비어 있는지(0xff 또는 빈 SPIFFS
표식만) 확인하고, 파일이나 모르는 내용이 있으면 지우지 않고 기록만 끈다(`bb_status` 5).
`coredump` 파티션이 없어서 패닉 백트레이스(`prior.core…`)는 남지 않는다. 리셋 사유와 재부팅
직전 흐름 기록(RTC 복사 8건)은 남는다.

## 기록

값은 v[0..3]. 표의 흐름 기록은 `Recovery` 유형(7)이다. 라이브러리가 이 유형의 최근 8건을 RTC에도
복사하므로, 패닉·워치독 재부팅 직전 기록이 flash에 못 내려갔어도 다음 부팅에서 원래 부팅 ID·uptime으로
다시 기록된다(중복 가능, 전원 차단 시에는 보존되지 않는다).

| 텍스트 | 값 |
|---|---|
| `tm.tag.fetch` | 태그 역할 조회(HTTP) 걸린 ms, 역할 첫 글자(ASCII: p/t/g/r, 0 = 없음), 메인 리더 여부 |
| `tm.tag.diff` | 로그인한 것과 다른 카드가 들어옴. 메인 리더 여부 |
| `tm.login` | 진행 시작. 역할(ASCII P/T/G), 메인 리더 여부 |
| `tm.lock.done` | 플레이어 잠금 완료 |
| `tm.open` | 문 열림. 1 플레이어, 2 술래, 3 유령(잠긴 문), 4 유령(열린 문), 뉴비 모드는 +10 |
| `tm.fail` | 진행 실패(카드 이탈). 1 잠금, 2 해제, 3 유령(열린 문), 4 유령(잠긴 문), 12 뉴비 술래 |
| `tm.cancel` | 서버 상태가 바뀌어 진행 취소 |
| `tm.open.server` | 서버 지시(`open`)로 열림 |
| `tm.debuff.end` | 디버프 60초 종료 |
| `tm.beetle.init` | Beetle이 `W`(부팅 핸드셰이크)를 보냄. 0 서브, 1 메인 |
| `tm.beetle.reset` | Beetle이 `R`을 보냄. 0 서브, 1 메인 |
| `tm.mp3.absent` | 부팅 때 DFPlayer 초기화 실패 |
| `tm.mp3.err` | DFPlayer 무응답 3회 초과(서버에 `MP3` 보고) |
| `tm.http.slow` | 서버 폴링 한 번(`has2wifi.Loop`)이 3초 이상 걸림. 걸린 ms, Wi-Fi 상태, RSSI. HTTP뿐 아니라 그 안에서 도는 60초 주기 Wi-Fi 스캔(1.5~4초), 상태 전환 연출, OTA도 포함한다 |
| `ota.github.begin` | OTA 직전 free heap, 최대 연속 블록, MQTT 세션 종료 여부, 기다린 ms |
| `ota.github.returned` | `ota.check()`가 업데이트 없이 돌아옴. free heap, 걸린 ms |

그 밖의 기록:

| 유형 / 텍스트 | 값 |
|---|---|
| `Boot` `device.boot` | 리셋 사유(`esp_reset_reason`), 펌웨어 버전, 파티션 버전 |
| `State` 상태 문자열 | v[0] = 0 game_state / 1 device_state 전환 |
| `Progress` `main.loop` | 60초 동안 loop가 가장 오래 멈춘 ms, loop 횟수, 가드 없이 보낸 출력 누계 |
| `HttpSummary` `http.loop` | 60초 동안 서버 폴링 횟수, 최대 ms, 합계 ms, 3초 이상 횟수 |
| `Health` `memory.main` | free heap, 최소 heap, 최대 연속 블록, loop 스택 여유 |
| `WifiSummary` `wifi` | Wi-Fi 상태, RSSI (`INT32_MIN` = 모름) |
| `WifiEvent` | 연결/끊김/IP (라이브러리 `BlackBoxWifi.h`) |

카드 ID·플레이어 이름·서버 응답 내용은 남기지 않는다.

## 게임에 주는 영향

- **업로드**: `game_state`를 아직 못 받았거나 `activate`인 동안, 태그 진행 중(`loginDone`),
  Wi-Fi가 끊긴 동안, OTA 중에는 올리지 않는다. 게임이 끝나면 밀린 기록을 올린다.
- **네오픽셀 / DFPlayer**: flash 쓰기가 LED 전송이나 소프트웨어 시리얼 전송 중간에 끼면 깨질 수
  있어 모든 `show()`와 DFPlayer 재생 명령이 블랙박스 writer와 같은 가드를 잡고 나간다. LED는 최대
  30 ms, 재생 명령은 최대 500 ms 기다리고, 그래도 못 잡으면 가드 없이 보낸다(횟수는
  `bb_forced_outputs`). DFPlayer 초기화는 기록을 시작하기 전에 끝낸다.
- **기록 호출**: `log()`는 대기 없는 큐 넣기다. flash 쓰기는 별도 태스크가 한다. 큐(32건)가 차면
  버리고 개수를 센다(`bb_dropped`).
- **OTA**: `device_state`가 `github`이 되면 업로드를 닫고 MQTT 세션이 메모리를 돌려줄 때까지
  (최대 15초) 기다린 뒤, flash 기록을 멈춘 상태에서 `ota.check()`를 돌린다.
- 블랙박스 태스크는 워치독을 먹이지 않고, 블랙박스가 재부팅을 일으키지도 않는다.

## 확인

텔넷(23번) 접속 직후 짧은 줄 네 개가 나온다(이 콘솔은 약 64바이트가 넘는 줄을 내보내지 못한다).

```
[bb] fw=8 heap=… min=… block=…
[bb] ready=1 status=1 part=1507328 claim=0
[bb] flash_err=0 up=… up_fail=0 drop=0
[bb] accepted=… persisted=… forced=0
```

`status`는 1 = 정상, 2 = 파티션 크기 거부, 5 = 모르는 내용이 있어 손대지 않음. `up`은 수집 PC가
ACK한 업로드 수다.

`spiffs`에 옛 내용이 남아 `status`가 5로 나오면(다른 파티션 표로 구운 적이 있는 장치), 펌웨어가
부팅 때 `spiffs` 전체를 한 번 지우고 재시작한다(약 15~20초). 이 펌웨어는 `spiffs`를 쓰지 않으므로
잃는 것이 없고, 유효한 링이 있으면 지우지 않는다. `claim`은 0 = 필요 없음, 2 = 실패, 3 = 한 번
지웠는데도 그대로라 다시 하지 않음.

배포 뒤 대기 상태의 한 대가 수집 PC SQLite에 저장되는지, `memory.main`의 heap 여유, LED 깨짐과
효과음 누락 여부를 먼저 확인한다.
