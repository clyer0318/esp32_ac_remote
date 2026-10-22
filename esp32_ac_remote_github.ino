#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_MitsubishiHeavy.h>

// 請填入使用者自己的 Wi-Fi 資料；不要把真實密碼上傳到 GitHub。
const char* ssid = "YOUR_WIFI_SSID";  // TODO: 填入自己的 Wi-Fi 名稱
const char* password = "YOUR_WIFI_PASSWORD";  // TODO: 填入自己的 Wi-Fi 密碼

WebServer server(80);
const uint16_t kIrLed = 4;
IRMitsubishiHeavy152Ac ac(kIrLed);

// --- 冷氣狀態變數 ---
int currentTemp = 25;
int modeIdx = 0;   // 0=冷氣, 1=除濕, 2=送風
int fanIdx = 0;    // 0=自動, 1, 2, 3, 4段
int swingIdx = 0;  // 0=自動擺動, 1=固定

String modeNames[] = {"❄️ 冷氣", "💧 除濕", "💨 送風"};
String fanNames[] = {"自動", "1段", "2段", "3段", "4段"};
String swingNames[] = {"自動擺動", "固定角度"};

unsigned long timerDuration = 0;
unsigned long timerStartTime = 0;
bool isTimerActive = false;
float timerHoursSetting = 1.0; 

// --- 發射紅外線的統一函式 ---
void updateAC() {
  if (modeIdx == 0) ac.setMode(1);
  else if (modeIdx == 1) ac.setMode(2);
  else if (modeIdx == 2) ac.setMode(3);

  ac.setFan(fanIdx);
  ac.setSwingVertical(swingIdx);
  ac.setTemp(currentTemp);
  ac.send();
}

// --- 網頁前端 (HTML + CSS + JS) ---
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <title>頂級冷氣遙控</title>
  <style>
    body { font-family: -apple-system, sans-serif; background: #f0f2f5; display: flex; justify-content: center; margin: 0; padding: 15px; }
    .remote { background: white; width: 100%; max-width: 400px; border-radius: 20px; box-shadow: 0 5px 20px rgba(0,0,0,0.1); padding: 20px; text-align: center; }
    .screen { background: #112233; color: white; border-radius: 15px; padding: 20px; margin-bottom: 20px; }
    .temp { font-size: 55px; font-weight: bold; margin: 10px 0; }
    .status-bar { display: flex; justify-content: space-around; font-size: 14px; color: #4ce; background: rgba(255,255,255,0.1); padding: 8px; border-radius: 10px; }
    
    button { 
      border: 1px solid #ddd; border-radius: 15px; padding: 15px 5px; 
      font-size: 16px; font-weight: bold; cursor: pointer; 
      -webkit-appearance: none; appearance: none; 
      background: #fff; color: #333; box-shadow: 0 2px 5px rgba(0,0,0,0.05);
    }
    button:active { background: #f0f0f0; transform: scale(0.98); }
    
    .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 12px; }
    .grid-3 { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px; margin-bottom: 20px; }
    
    .btn-on { background: #d4edda; border-color: #c3e6cb; color: #155724; }
    .btn-off { background: #f8d7da; border-color: #f5c6cb; color: #721c24; }
    .btn-setting { display: flex; flex-direction: column; align-items: center; gap: 5px; font-size: 14px;}
    .val { color: #007bff; font-size: 16px; }
    
    .timer-card { background: #fff3cd; border-radius: 15px; padding: 15px; border: 1px solid #ffeeba; }
    input[type="number"] { width: 60px; height: 35px; font-size: 16px; text-align: center; border-radius: 8px; border: 1px solid #ccc; margin: 0 5px; }
    .btn-timer { background: #ffc107; border-color: #ffc107; padding: 10px 15px; }
  </style>
  <script>
    function send(cmd) { fetch(cmd).then(() => location.reload()); }
    function setTimer() {
      let h = document.getElementById('hours').value;
      if (h > 0) fetch('/setTimer?h=' + h).then(() => location.reload());
    }
  </script>
</head>
<body>
  <div class="remote">
    <div class="screen">
      <div style="font-size:12px; color:#aaa; letter-spacing: 2px;">MITSUBISHI HEAVY</div>
      <div class="temp">%TEMP%°C</div>
      <div class="status-bar">
        <span>%MODE%</span><span>%FAN%</span><span>%SWING%</span>
      </div>
    </div>
    
    <div class="grid-2">
      <button class="btn-on" onclick="send('/on')">開冷氣 🟢</button>
      <button class="btn-off" onclick="send('/off')">關冷氣 🔴</button>
      <button onclick="send('/tempUp')">▲ 調高</button>
      <button onclick="send('/tempDown')">▼ 調低</button>
    </div>
    
    <div class="grid-3">
      <button class="btn-setting" onclick="send('/mode')">模式<span class="val">%MODE%</span></button>
      <button class="btn-setting" onclick="send('/fan')">風量<span class="val">%FAN%</span></button>
      <button class="btn-setting" onclick="send('/swing')">風向<span class="val">%SWING%</span></button>
    </div>
    
    <div class="timer-card">
      <div style="font-weight:bold; color:#856404; margin-bottom:10px;">⏳ 定時關機設定</div>
      <div>
        <input type="number" id="hours" min="0.5" step="0.5" max="24" value="1">小時
        <button class="btn-timer" onclick="setTimer()">啟動</button>
      </div>
      <div style="margin-top:10px; color:#d32f2f; font-weight:bold;">%TIMER_STATUS%</div>
    </div>
  </div>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  ac.begin();
  ac.on();
  updateAC();
  
  // ===== 固定 IP =====
// TODO: 若使用固定 IP，請依自己的路由器網段修改以下 IP 與閘道，並避開已使用的位址。
IPAddress local_IP(192, 168, 10, 200);  // 範例固定 IP
IPAddress gateway(192, 168, 10, 1);  // 範例路由器閘道
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
  Serial.println("❌ 固定 IP 設定失敗");
}

// ===== 連接 Wi-Fi =====
WiFi.begin(ssid, password);

while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  Serial.print(".");
}

Serial.println("\n✅ Wi-Fi 連線成功");
Serial.println("網址: http://" + WiFi.localIP().toString());

  server.on("/", []() {
    String page = htmlPage;
    page.replace("%TEMP%", String(currentTemp));
    page.replace("%MODE%", modeNames[modeIdx]);
    page.replace("%FAN%", fanNames[fanIdx]);
    page.replace("%SWING%", swingNames[swingIdx]);
    
    if (isTimerActive) {
      page.replace("%TIMER_STATUS%", "⚠️ 已啟動：" + String(timerHoursSetting) + " 小時後將自動關機");
    } else {
      page.replace("%TIMER_STATUS%", "目前無定時");
    }
    // 👇 這裡已經幫你加入 utf-8 防亂碼咒語了！
    server.send(200, "text/html; charset=utf-8", page);
  });

  server.on("/on", []() { ac.on(); updateAC(); isTimerActive = false; server.send(200, "text/plain", "OK"); });
  server.on("/off", []() { ac.off(); ac.send(); isTimerActive = false; server.send(200, "text/plain", "OK"); });
  server.on("/tempUp", []() { if(currentTemp < 30) currentTemp++; updateAC(); server.send(200, "text/plain", "OK"); });
  server.on("/tempDown", []() { if(currentTemp > 18) currentTemp--; updateAC(); server.send(200, "text/plain", "OK"); });
  
  server.on("/mode", []() { modeIdx = (modeIdx + 1) % 3; updateAC(); server.send(200, "text/plain", "OK"); });
  server.on("/fan", []() { fanIdx = (fanIdx + 1) % 5; updateAC(); server.send(200, "text/plain", "OK"); });
  server.on("/swing", []() { swingIdx = (swingIdx == 0) ? 1 : 0; updateAC(); server.send(200, "text/plain", "OK"); });
  
  server.on("/setTimer", []() {
    if (server.hasArg("h")) {
      timerHoursSetting = server.arg("h").toFloat();
      timerDuration = (unsigned long)(timerHoursSetting * 3600000.0);
      timerStartTime = millis();
      isTimerActive = true;
    }
    server.send(200, "text/plain", "OK");
  });

  server.begin();
}

void loop() {
  server.handleClient();
  if (isTimerActive && (millis() - timerStartTime >= timerDuration)) {
    ac.off();
    ac.send();
    isTimerActive = false;
    Serial.println("⏰ 定時時間到，已經自動關機！");
  }
}