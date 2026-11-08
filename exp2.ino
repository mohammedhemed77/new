#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ================= 1. إعدادات الشبكة والأطراف =================
const char* WIFI_SSID = "Hemed";     // اكتب اسم الواي فاي هنا
const char* WIFI_PASS = "12345678"; // اكتب باسورد الواي فاي هنا

#define TEMP_SENSOR_PIN 4    // حساس الحرارة DS18B20
#define RELAY_FAN_PIN   25   // ريليه المروحة
#define RELAY_TV_PIN    26   // ريليه الشاشة
#define RELAY_PC_PIN    27   // ريليه زر باور الكمبيوتر

#define RELAY_ON  LOW 
#define RELAY_OFF HIGH

// ================= 2. متغيرات النظام والذاكرة =================
WebServer server(80);
Preferences prefs;
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature sensors(&oneWire);

enum FanMode { FAN_AUTO = 0, FAN_MANUAL_ON = 1, FAN_MANUAL_OFF = 2 };
FanMode currentFanMode = FAN_AUTO;

float tempHigh = 38.0; // درجة تشغيل المروحة
float tempLow  = 34.0; // درجة فصل المروحة
float currentTemp = 0.0;

bool fanState = false;
bool tvState  = false;
bool pcPulseActive = false;

unsigned long lastTempReadTime = 0;
unsigned long pcPulseStartTime = 0;

// ================= 3. كود واجهة الويب (HTML + CSS + JS) =================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html dir="rtl" lang="ar">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>لوحة التحكم الذكية</title>
  <style>
    :root { --bg: #0f172a; --card: #1e293b; --text: #f8fafc; --accent: #3b82f6; --green: #10b981; --red: #ef4444; }
    body { font-family: system-ui, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 15px; text-align: center; }
    h1 { font-size: 1.4rem; margin-bottom: 15px; color: #94a3b8; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 15px; max-width: 900px; margin: auto; }
    .card { background: var(--card); border-radius: 14px; padding: 18px; box-shadow: 0 4px 10px rgba(0,0,0,0.3); }
    .temp-box { font-size: 2.6rem; font-weight: bold; color: #38bdf8; margin: 10px 0; }
    .badge { display: inline-block; padding: 4px 12px; border-radius: 20px; font-size: 0.85rem; font-weight: bold; }
    .on { background: rgba(16,185,129,0.2); color: var(--green); }
    .off { background: rgba(239,68,68,0.2); color: var(--red); }
    .btn-group { display: flex; gap: 8px; margin-top: 12px; }
    button { flex: 1; padding: 12px; border: none; border-radius: 8px; font-size: 0.95rem; font-weight: bold; cursor: pointer; color: white; background: #334155; transition: 0.2s; }
    button:active { transform: scale(0.97); }
    button.active-mode { background: var(--accent); }
    button.btn-green { background: var(--green); }
    button.btn-red { background: var(--red); }
    button.btn-pc { background: #8b5cf6; width: 100%; margin-top: 12px; }
    .inputs { display: flex; gap: 10px; margin-top: 15px; align-items: center; justify-content: center; }
    input { width: 65px; padding: 8px; border-radius: 6px; border: 1px solid #475569; background: var(--bg); color: white; text-align: center; font-size: 1rem; }
  </style>
</head>
<body>
  <h1>لوحة التحكم الذكية بالنظام</h1>
  <div class="grid">
    <!-- كارت الحرارة والمروحة -->
    <div class="card">
      <h3>🌡️ درجة الحرارة والمروحة</h3>
      <div class="temp-box"><span id="temp">--</span> °C</div>
      <div>حالة المروحة: <span id="fanStatus" class="badge off">متوقفة</span></div>
      <div class="btn-group">
        <button id="btnAuto" onclick="sendCmd('fanMode', 0)">أوتوماتيك</button>
        <button id="btnOn" onclick="sendCmd('fanMode', 1)">تشغيل مستمر</button>
        <button id="btnOff" onclick="sendCmd('fanMode', 2)">إيقاف</button>
      </div>
      <hr style="border-color:#334155; margin:15px 0;">
      <div style="font-size:0.85rem; color:#94a3b8;">ضبط حرارة الأوتوماتيك (تشغيل / فصل)</div>
      <div class="inputs">
        <label>تشغيل:</label><input type="number" step="0.5" id="tHigh">
        <label>فصل:</label><input type="number" step="0.5" id="tLow">
        <button style="flex:0.6; padding:8px; background:var(--accent);" onclick="saveThresh()">حفظ</button>
      </div>
    </div>

    <!-- كارت الشاشة -->
    <div class="card">
      <h3>📺 التحكم في الشاشة</h3>
      <p style="color:#94a3b8; font-size:0.9rem;">قطع وتوصيل التيار المباشر للشاشة</p>
      <div style="margin: 20px 0;">الحالة الآن: <span id="tvStatus" class="badge off">مغلقة</span></div>
      <div class="btn-group">
        <button class="btn-green" onclick="sendCmd('tv', 1)">تشغيل الشاشة</button>
        <button class="btn-red" onclick="sendCmd('tv', 0)">فصل الشاشة</button>
      </div>
    </div>

    <!-- كارت الكمبيوتر -->
    <div class="card">
      <h3>💻 التحكم في الكمبيوتر (PC)</h3>
      <p style="color:#94a3b8; font-size:0.9rem;">يرسل نبضة (0.5 ثانية) لزر الباور لتشغيل الجهاز أو عمل Shutdown آمن</p>
      <button class="btn-pc" onclick="triggerPC()">⚡ ضغطة زر باور الكمبيوتر</button>
      <div id="pcMsg" style="margin-top:10px; font-size:0.85rem; color:#a78bfa; height:20px;"></div>
    </div>
  </div>

<script>
  let firstLoad = true;
  function fetchStatus() {
    fetch('/status').then(r => r.json()).then(data => {
      document.getElementById('temp').innerText = data.temp.toFixed(1);
      
      const fanBadge = document.getElementById('fanStatus');
      fanBadge.innerText = data.fanState ? 'تعمل الآن' : 'متوقفة';
      fanBadge.className = 'badge ' + (data.fanState ? 'on' : 'off');

      const tvBadge = document.getElementById('tvStatus');
      tvBadge.innerText = data.tvState ? 'تعمل الآن' : 'مغلقة';
      tvBadge.className = 'badge ' + (data.tvState ? 'on' : 'off');

      document.getElementById('btnAuto').className = (data.fanMode == 0) ? 'active-mode' : '';
      document.getElementById('btnOn').className   = (data.fanMode == 1) ? 'active-mode' : '';
      document.getElementById('btnOff').className  = (data.fanMode == 2) ? 'active-mode' : '';

      if(firstLoad) {
        document.getElementById('tHigh').value = data.tHigh;
        document.getElementById('tLow').value = data.tLow;
        firstLoad = false;
      }
    }).catch(e => console.log(e));
  }

  function sendCmd(type, val) {
    fetch(`/action?type=${type}&val=${val}`).then(() => fetchStatus());
  }

  function saveThresh() {
    const h = document.getElementById('tHigh').value;
    const l = document.getElementById('tLow').value;
    fetch(`/setTemp?high=${h}&low=${l}`).then(() => alert('تم حفظ درجات الحرارة بنجاح!'));
  }

  function triggerPC() {
    if(confirm('هل تريد إرسال ضغطة لزر باور الكمبيوتر؟')) {
      sendCmd('pc', 1);
      const msg = document.getElementById('pcMsg');
      msg.innerText = 'تم إرسال النبضة لزر الباور بنجاح ✓';
      setTimeout(() => msg.innerText = '', 3000);
    }
  }

  setInterval(fetchStatus, 1500);
  fetchStatus();
</script>
</body>
</html>
)rawliteral";

// ================= 4. دوال التحكم والـ الهاردوير =================
void setRelay(int pin, bool state) {
  digitalWrite(pin, state ? RELAY_ON : RELAY_OFF);
}

void updateFanHardware() {
  if (currentFanMode == FAN_MANUAL_ON) {
    fanState = true;
  } else if (currentFanMode == FAN_MANUAL_OFF) {
    fanState = false;
  } else if (currentFanMode == FAN_AUTO) {
    if (currentTemp != -99.0) {
      if (currentTemp >= tempHigh) fanState = true;
      else if (currentTemp <= tempLow) fanState = false;
    } else {
      fanState = false; // إيقاف آمن في حال غياب الحساس وعدم وجود قراءة
    }
  }
  setRelay(RELAY_FAN_PIN, fanState);
}

void handleSensorsAndTimers() {
  // 1. قراءة الحرارة كل ثانيتين
  if (millis() - lastTempReadTime >= 2000) {
    lastTempReadTime = millis();
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t > -127.0) {
      currentTemp = t;
    } else {
      currentTemp = -99.0; // علامة على فصل السلك أو عدم وجود حساس
    }
    updateFanHardware();
  }

  // 2. فصل نبضة زر باور الكمبيوتر بعد 500 ملي ثانية
  if (pcPulseActive && (millis() - pcPulseStartTime >= 500)) {
    setRelay(RELAY_PC_PIN, false);
    pcPulseActive = false;
  }
}

// ================= 5. مسارات السيرفر (Routes) =================
void setupServerRoutes() {
  // الصفحة الرئيسية
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", index_html);
  });

  // إرسال البيانات بصيغة JSON للمتصفح
  server.on("/status", HTTP_GET, []() {
    String json = "{";
    json += "\"temp\":" + String(currentTemp) + ",";
    json += "\"fanMode\":" + String(currentFanMode) + ",";
    json += "\"fanState\":" + String(fanState ? "true" : "false") + ",";
    json += "\"tvState\":" + String(tvState ? "true" : "false") + ",";
    json += "\"tHigh\":" + String(tempHigh) + ",";
    json += "\"tLow\":" + String(tempLow);
    json += "}";
    server.send(200, "application/json", json);
  });

  // استقبال أوامر الأزرار
  server.on("/action", HTTP_GET, []() {
    String type = server.arg("type");
    int val = server.arg("val").toInt();

    if (type == "fanMode") {
      currentFanMode = (FanMode)val;
      prefs.putInt("fanMode", val); // حفظ الوضع في الذاكرة
      updateFanHardware();
    } 
    else if (type == "tv") {
      tvState = (val == 1);
      setRelay(RELAY_TV_PIN, tvState);
    } 
    else if (type == "pc" && !pcPulseActive) {
      setRelay(RELAY_PC_PIN, true);
      pcPulseStartTime = millis();
      pcPulseActive = true;
    }
    server.send(200, "text/plain", "OK");
  });

  // حفظ درجات الحرارة الجديدة في الذاكرة الدائمة
  server.on("/setTemp", HTTP_GET, []() {
    if (server.hasArg("high") && server.hasArg("low")) {
      tempHigh = server.arg("high").toFloat();
      tempLow  = server.arg("low").toFloat();
      prefs.putFloat("tHigh", tempHigh);
      prefs.putFloat("tLow", tempLow);
      updateFanHardware();
    }
    server.send(200, "text/plain", "Saved");
  });

  server.begin();
}

// ================= 6. الإعداد والتشغيل (Setup & Loop) =================
void setup() {
  Serial.begin(115200);
  sensors.begin();

  // تهيئة الأطراف ومنع اللقط العشوائي وقت التشغيل
  pinMode(RELAY_FAN_PIN, OUTPUT);
  pinMode(RELAY_TV_PIN,   OUTPUT);
  pinMode(RELAY_PC_PIN,   OUTPUT);
  
  digitalWrite(RELAY_FAN_PIN, RELAY_OFF);
  digitalWrite(RELAY_TV_PIN,  RELAY_OFF);
  digitalWrite(RELAY_PC_PIN,  RELAY_OFF);

  // استرجاع الإعدادات المحفوظة من ذاكرة الفلاش
  prefs.begin("sys-ctrl", false);
  currentFanMode = (FanMode)prefs.getInt("fanMode", 2); // يبدأ افتراضياً على وضع الإيقاف لحين التغيير
  tempHigh = prefs.getFloat("tHigh", 38.0);
  tempLow  = prefs.getFloat("tLow", 34.0);

  // الاتصال بالواي فاي
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP Address: " + WiFi.localIP().toString());

  // تفعيل اسم النطاق المحلي http://control.local
  if (MDNS.begin("control")) {
    Serial.println("mDNS responder started: http://control.local");
  }

  setupServerRoutes();
  updateFanHardware(); // تطبيق الحالة الأولية مباشرة
}

void loop() {
  server.handleClient();
  handleSensorsAndTimers();
}