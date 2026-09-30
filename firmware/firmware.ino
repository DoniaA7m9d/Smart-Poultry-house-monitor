#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

//  الإعدادات
const char* WIFI_SSID = "";
const char* WIFI_PASS = "";
const char* BOT_TOKEN = "";
const char* CHAT_ID   = "";
const char* SHEET_URL = "";
const char* PHONE     = "";

// معايرة MQ137 (قيم مبدئية لازم تعايريها)
const float RL = 10.0;      // kOhm 
float R0 = 10.0;            // قيمة Rs في هواء نظيف
const float NH3_A = 1.0;    // ثوابت المنحنى من الداتاشيت/المعايرة
const float NH3_B = -1.5;

//  الأطراف 
#define PIN_NH3 34
#define PIN_FAN 23
#define PIN_PUMP 19
#define PIN_SIREN 18
#define PIN_MANUAL 32
#define PIN_MAINS 33
#define MHZ_RX 16
#define MHZ_TX 17
#define SIM_RX 26
#define SIM_TX 27
#define R_ON  LOW
#define R_OFF HIGH

Adafruit_SHT31 sht;
HardwareSerial mhz(2), sim(1);
Preferences prefs;

float temp = NAN, hum = NAN, nh3 = 0, thi = 0;
int co2 = -1, ageDays = 1;
bool fanOn = false, pumpOn = false, mainsOk = true, forceFan = false;
long lastUpdateId = 0;
unsigned long tRead = 0, tLog = 0, tCmd = 0, tWifi = 0;
unsigned long lastAlert[6] = {0};

//  أدوات 
String urlEnc(const String& s) {
  String o; char b[4];
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.') o += c;
    else { sprintf(b, "%%%02X", (uint8_t)c); o += b; }
  }
  return o;
}

bool tgSend(const String& m) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h; h.setTimeout(8000);
  h.begin(c, String("https://api.telegram.org/bot") + BOT_TOKEN +
             "/sendMessage?chat_id=" + CHAT_ID + "&text=" + urlEnc(m));
  int code = h.GET(); h.end();
  return code == 200;
}

void smsSend(const String& m) {
  sim.println("AT+CMGF=1"); delay(500);
  sim.print("AT+CMGS=\""); sim.print(PHONE); sim.println("\""); delay(1000);
  sim.print(m); delay(300); sim.write(26); delay(3000);
  while (sim.available()) sim.read();
}

// id: 0 NH3, 1 حرارة, 2 CO2, 3 كهرباء, 4 عطل حساس
void alert(int id, const String& m) {
  if (lastAlert[id] != 0 && millis() - lastAlert[id] < 600000UL) return;
  lastAlert[id] = millis();
  tgSend(m);
  smsSend(m);   
}

//  القراءة 
int readCO2() {
  byte cmd[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79}, r[9];
  while (mhz.available()) mhz.read();
  mhz.write(cmd, 9);
  if (mhz.readBytes(r, 9) != 9 || r[0] != 0xFF || r[1] != 0x86) return -1;
  return r[2] * 256 + r[3];
}

float readNH3() {
  long sum = 0;
  for (int i = 0; i < 20; i++) { sum += analogReadMilliVolts(PIN_NH3); delay(5); }
  float v = (sum / 20.0) / 1000.0 * 1.5;       // تعويض المقسم 10k/20k
  if (v < 0.05) return 0;
  float rs = (5.0 - v) / v * RL;
  return NH3_A * pow(rs / R0, NH3_B);
}

float calcTHI(float t, float rh) {             // 0.85*Tdb + 0.15*Twb
  float tw = t * atan(0.151977 * sqrt(rh + 8.313659)) + atan(t + rh)
           - atan(rh - 1.676331) + 0.00391838 * pow(rh, 1.5) * atan(0.023101 * rh)
           - 4.686035;
  return 0.85 * t + 0.15 * tw;
}

void readSensors() {
  temp = sht.readTemperature();
  hum = sht.readHumidity();
  nh3 = readNH3();
  if (millis() > 180000UL) co2 = readCO2();    // تسخين MH-Z19B
  mainsOk = digitalRead(PIN_MAINS) == HIGH;
  if (!isnan(temp) && !isnan(hum)) thi = calcTHI(temp, hum);
}

//  التحكم 
void control() {
  bool manual = digitalRead(PIN_MANUAL) == LOW;
  bool fail = isnan(temp) || isnan(hum);
  float target = max(24.0f, 33.0f - 0.4f * ageDays);

  if (manual || fail || forceFan) fanOn = true;
  else {
    bool need  = temp > target + 1 || nh3 > 20 || co2 > 3000;
    bool clear = temp < target && nh3 < 15 && (co2 < 2500);
    if (need) fanOn = true; else if (clear) fanOn = false;
  }

  if (manual || fail) pumpOn = false;
  else {
    // خلايا التبريد فقط لو الرطوبة مش عالية
    bool needP  = (temp > target + 4 || thi >= 29) && hum < 75;
    bool clearP = temp < target + 2 || hum > 80;
    if (needP && fanOn) pumpOn = true; else if (clearP) pumpOn = false;
  }
  digitalWrite(PIN_FAN,  fanOn  ? R_ON : R_OFF);
  digitalWrite(PIN_PUMP, pumpOn ? R_ON : R_OFF);
}

void checkAlerts() {
  float target = max(24.0f, 33.0f - 0.4f * ageDays);
  bool siren = false;
  if (nh3 > 25) { siren = true; alert(0, "ALERT: Ammonia " + String(nh3, 0) + " ppm"); }
  if (!isnan(temp) && (temp > target + 5 || thi >= 31)) {
    siren = true;
    alert(1, "ALERT: Heat stress T=" + String(temp, 1) + "C THI=" + String(thi, 1));
  }
  if (co2 > 3500) { siren = true; alert(2, "ALERT: CO2 " + String(co2) + " ppm"); }
  if (!mainsOk) { siren = true; alert(3, "ALERT: Mains power lost (on battery)"); }
  if (isnan(temp) || isnan(hum)) { siren = true; alert(4, "ALERT: Temp/Hum sensor failure, fans forced ON"); }
  digitalWrite(PIN_SIREN, siren ? R_ON : R_OFF);
}

//  جوجل شيت 
void logSheet() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h; h.setTimeout(8000);
  h.begin(c, SHEET_URL);
  h.addHeader("Content-Type", "application/json");
  String j = "{\"temp\":" + String(temp, 1) + ",\"hum\":" + String(hum, 1) +
             ",\"thi\":" + String(thi, 1) + ",\"nh3\":" + String(nh3, 1) +
             ",\"co2\":" + String(co2) + ",\"fan\":" + String(fanOn) +
             ",\"pump\":" + String(pumpOn) + ",\"mains\":" + String(mainsOk) + "}";
  h.POST(j);     // تجاهل رد التحويل (302)، البيانات بتتسجل
  h.end();
}

//  أوامر تليجرام 
String statusText() {
  return "T=" + String(temp, 1) + "C RH=" + String(hum, 0) + "% THI=" + String(thi, 1) +
         "\nNH3=" + String(nh3, 1) + "ppm CO2=" + String(co2) +
         "\nFan=" + String(fanOn) + " Pump=" + String(pumpOn) +
         " Mains=" + String(mainsOk) + " Age=" + String(ageDays) + "d";
}

void pollTelegram() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h; h.setTimeout(6000);
  h.begin(c, String("https://api.telegram.org/bot") + BOT_TOKEN +
             "/getUpdates?limit=1&timeout=0&offset=" + String(lastUpdateId + 1));
  if (h.GET() != 200) { h.end(); return; }
  String p = h.getString(); h.end();
  int i = p.indexOf("\"update_id\":");
  if (i < 0) return;
  lastUpdateId = p.substring(i + 12, p.indexOf(',', i)).toInt();
  int t = p.indexOf("\"text\":\"");
  if (t < 0 || p.indexOf(String("\"id\":") + CHAT_ID) < 0) return;
  String txt = p.substring(t + 8, p.indexOf('"', t + 8));

  if (txt.startsWith("/status")) tgSend(statusText());
  else if (txt.startsWith("/fan_on")) { forceFan = true; control(); tgSend("Fans forced ON"); }
  else if (txt.startsWith("/auto")) { forceFan = false; control(); tgSend("Auto mode"); }
  else if (txt.startsWith("/age ")) {
    ageDays = constrain(txt.substring(5).toInt(), 1, 60);
    prefs.putInt("age", ageDays);
    tgSend("Age set to " + String(ageDays) + " days");
  }
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED || millis() - tWifi < 30000UL) return;
  tWifi = millis();
  WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_FAN, OUTPUT);   digitalWrite(PIN_FAN, R_OFF);
  pinMode(PIN_PUMP, OUTPUT);  digitalWrite(PIN_PUMP, R_OFF);
  pinMode(PIN_SIREN, OUTPUT); digitalWrite(PIN_SIREN, R_OFF);
  pinMode(PIN_MANUAL, INPUT_PULLUP);
  pinMode(PIN_MAINS, INPUT);
  analogReadResolution(12);
  Wire.begin(21, 22);
  sht.begin(0x44);
  mhz.begin(9600, SERIAL_8N1, MHZ_RX, MHZ_TX); mhz.setTimeout(1000);
  sim.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX);
  prefs.begin("poultry", false);
  ageDays = prefs.getInt("age", 1);
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = {.timeout_ms = 60000, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&cfg);
#else
  esp_task_wdt_init(60, true);
#endif
  esp_task_wdt_add(NULL);
}

void loop() {
  esp_task_wdt_reset();
  unsigned long now = millis();
  if (now - tRead >= 5000)   { tRead = now; readSensors(); control(); checkAlerts(); }
  if (now - tLog  >= 300000UL) { tLog = now; logSheet(); }
  if (now - tCmd  >= 5000)   { tCmd = now; pollTelegram(); }
  ensureWifi();
}
