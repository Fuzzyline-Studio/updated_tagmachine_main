void DataChanged()
{
  static StaticJsonDocument<1000> cur;  //저장되어 있는 cur과 읽어온 my 값과 비교후 실행
  if (my["brightness"].as<int>() != cur["brightness"].as<int>()) {
    UpdateBrightness();
  }
  if((String)(const char*)my["game_state"] != (String)(const char*)cur["game_state"]){
    if((String)(const char*)my["game_state"] == "setting"){
        strCurState = "setting";
        SettingFunc();
    }
    else if((String)(const char*)my["game_state"] == "ready"){
        strCurState = "ready";
        ReadyFunc();
    }
    else if((String)(const char*)my["game_state"] == "activate"){
        strCurState = "activate";
        ActivateFunc();
    }
  }
  if((String)(const char*)my["device_state"] != (String)(const char*)cur["device_state"]){  
    String deviceState = (String)(const char*)my["device_state"];
    if(deviceState == "lock"){
      strCurState = deviceState;
      if(loginDone) QueuePendingDeviceState(deviceState);
      else ApplyDeviceState(deviceState);
    }
    else if(deviceState == "mo"){
        digitalWrite(RELAY_PIN, HIGH);
        delay(1000);
        digitalWrite(RELAY_PIN, LOW);
        has2wifi.Send((String)(const char*)my["device_name"], "device_state", (String)(const char*)cur["device_state"]);
    }
    else if(deviceState == "open"){
        digitalWrite(RELAY_PIN, HIGH);
        delay(1000);
        digitalWrite(RELAY_PIN, LOW);
        has2wifi.Send((String)(const char*)my["device_name"], "device_state", "activate");
    }
    else if(deviceState == "activate"){
        strCurState = deviceState;
        if(loginDone) QueuePendingDeviceState(deviceState);
        else ApplyDeviceState(deviceState);
    }
    else if(deviceState == "github"){
        // esp_task_wdt_delete(NULL);  // [WDT 비활성화]
        ota.check();
        // esp_task_wdt_add(NULL);  // [WDT 비활성화]
    }
    else if(deviceState == "tagger"){
        ApplyDeviceState("tagger");
    }
  }
  if((String)(const char*)my["device_state"] == "debuff"){ 
    ApplyDeviceState("debuff");
  }
  GameSetting();
  NewbieModeSetting();
  {
    String _ds = (String)(const char*)my["device_state"];
    if (_ds == "lock" || _ds == "activate" || _ds == "debuff") strCurState = _ds;
  }
  cur = my; // cur 데이터 그룹에 현재 읽어온 데이터 저장
}

void QueuePendingDeviceState(String deviceState) {
    pendingDeviceState = deviceState;
    pendingDeviceStateApply = true;
    Serial.println("Queue Device State: " + deviceState);
}

/**
 * @brief 펌웨어가 스스로 상태 전이를 끝냈을 때, 진행 중 DataChanged()가 큐에 넣어 둔
 *        (이미 지나간) device_state를 버린다.
 *
 * 잠금/해제 타이머 함수들은 완료 직전에 ReceiveMine()+DataChanged()로 서버 상태를 재확인하는데,
 * 이때는 loginDone==true 라서 감지된 상태 변화가 즉시 적용되지 않고 큐에 들어간다.
 * 그런데 직전 전이가 cur 에 반영되지 않은 경우(예: 잠금 완료 시 자기 자신이 보낸 lock 을
 * ReceiveMine()으로만 읽고 DataChanged()를 거치지 않아 cur 이 activate 로 남음)에는 이 재확인이
 * "activate→lock" 을 뒤늦게 감지해 "lock" 을 큐에 남긴다. 해제가 끝나 loginDone 이 풀리면
 * ApplyPendingDeviceState()가 그 낡은 lock 을 적용해, 노랑(activate)이어야 할 네오픽셀이
 * 초록(lock)으로 덮이고 릴레이까지 다시 잠긴다(하드모드 잠금→해제→잠금→해제 증상).
 *
 * 성공 분기에서는 서버 상태가 기대한 값임을 방금 확인했고, 그 상태에서 벗어나는 전이를
 * 펌웨어 자신이 보내므로 큐에 남은 값은 항상 낡은 값이다. 버려도 안전하다.
 */
void ClearPendingDeviceState() {
    if (pendingDeviceStateApply)
        Serial.println("Drop stale pending state: " + pendingDeviceState);
    pendingDeviceState = "";
    pendingDeviceStateApply = false;
}

void ApplyPendingDeviceState() {
    if (!pendingDeviceStateApply || loginDone) return;

    String deviceState = pendingDeviceState;
    pendingDeviceState = "";
    pendingDeviceStateApply = false;
    ApplyDeviceState(deviceState);
}

void ApplyDeviceState(String deviceState) {
    if(deviceState == "lock"){
      strCurState = "lock";
      AllNeoOn(GREEN);
      digitalWrite(RELAY_PIN, LOW);            // 도어 잠금(닫힘) - tagger 등 오픈 상태에서 복귀 시 보장
    }
    else if(deviceState == "activate"){
        strCurState = "activate";
        ActivateFunc();
    }
    else if(deviceState == "debuff"){
        pendingDeviceState = "";
        pendingDeviceStateApply = false;
        strCurState = "debuff";
        ptrRfidFail = WaitFunc;
        AllNeoOn(PURPLE);
        digitalWrite(RELAY_PIN, LOW);            // 디버프 대기 중 도어 잠금(닫힘)
        Serial.println("디버프 시작");
        DebuffTimer.deleteTimer(debuffTimerId);
        debuffTimerId = DebuffTimer.setInterval(60000,DebuffTimerFunc);
        ReturnNormalState();
    }
    else if(deviceState == "tagger"){
        pendingDeviceState = "";
        pendingDeviceStateApply = false;
        strCurState = "tagger";
        AllNeoOn(PURPLE);                       // 전체 보라색
        digitalWrite(RELAY_PIN, HIGH);          // 도어 오픈, 계속 유지
        WifiTimer.deleteTimer(wifiTimerId);
        wifiTimerId = WifiTimer.setInterval(2000,WifiIntervalFunc);
        ptrCurrentMode = WhichTagged;           // 태그 읽기 활성화(상태와 무관하게 보장)
        ptrRfidMain   = CommnunicationMainBeetle;
        ptrRfidSub    = CommnunicationBeetle;
        ptrRfidMode   = Login;
        ptrRfidFail   = WaitFunc;
    }
}

/**
 * @brief device_state 전송 직전 서버가 tagger로 바뀌었는지 확인 (진행 중 수신된 tagger 명령 유실 방지)
 * @return true면 tagger 적용 완료 - 호출측은 이후 전송/연출을 건너뛰어야 함
 */
bool TaggerOverrideCheck(){
    has2wifi.ReceiveMine();
    if((String)(const char*)my["device_state"] == "tagger"){
        ApplyDeviceState("tagger");
        SubSerialFlush();
        MainSerialFlush();
        return true;
    }
    return false;
}
void WaitFunc(){

}
void SettingFunc(void){
    Serial.println("SETTING");
    DebuffTimer.deleteTimer(debuffTimerId);
    SubSerialTimer.deleteTimer(subSerialTimerId);
    GameTimer.deleteTimer(gameTimerId);
    WifiTimer.deleteTimer(wifiTimerId);                                          //게임 타이머 종료
    wifiTimerId = WifiTimer.setInterval(2000,WifiIntervalFunc);
    
    AllNeoOn(WHITE);
    digitalWrite(RELAY_PIN, HIGH);
    ptrCurrentMode = WaitFunc;
    ptrRfidMode = WaitRfid;
    GameSetting();
}
void ActivateFunc(void){
    Serial.println("ACTIVATE");
    DebuffTimer.deleteTimer(debuffTimerId);
    SubSerialTimer.deleteTimer(subSerialTimerId);
    GameTimer.deleteTimer(gameTimerId);
    WifiTimer.deleteTimer(wifiTimerId);                                          //게임 타이머 종료
    wifiTimerId = WifiTimer.setInterval(2000,WifiIntervalFunc);

    AllNeoOn(YELLOW);
    digitalWrite(RELAY_PIN, LOW);
    ptrCurrentMode = WhichTagged;
    ptrRfidMode = Login;
    ptrRfidFail = WaitFunc;
    ptrRfidMain = CommnunicationMainBeetle;
    ptrRfidSub = CommnunicationBeetle;
    GameSetting();
    NewbieModeSetting();
    if ((String)(const char*)my["mode"] == "easy") {
        has2wifi.Send((String)(const char*)my["device_name"], "device_state", "lock");
        my["device_state"] = "lock";
        strCurState = "lock";
        AllNeoOn(GREEN);
    }
}
void ReadyFunc(void){
    Serial.println("READY");
    DebuffTimer.deleteTimer(debuffTimerId);
    SubSerialTimer.deleteTimer(subSerialTimerId);
    GameTimer.deleteTimer(gameTimerId);
    WifiTimer.deleteTimer(wifiTimerId);                                          //게임 타이머 종료
    wifiTimerId = WifiTimer.setInterval(2000,WifiIntervalFunc);

    AllNeoOn(RED);
    digitalWrite(RELAY_PIN, HIGH);
    ptrCurrentMode = WaitFunc;
    ptrRfidMode = WaitRfid;
    GameSetting();
}
void GameSetting(){
    playerLockTime = (int)my["player_lock_time"];
    playerUnlockTime = (int)my["player_unlock_time"];
    taggerUnlockTime = (int)my["tagger_unlock_time"];
    ghostOpenTime = (int)my["ghost_open_time"];
}
void NewbieModeSetting() {
    if ((String)(const char*)my["mode"] == "easy" &&
        (String)(const char*)my["game_state"] == "activate" &&
        (String)(const char*)my["device_state"] != "tagger") {
        ptrRfidMode = NewbieLogin;
    }
}
