// DeskBuddy port for the ORIGINAL M5StickC (ESP32-PICO-D4, 80x160 ST7735S TFT).
// Target: Arduino IDE board "M5StickC"; install the M5StickC library.
// Orientation: landscape 160x80. The AXP192 power button toggles eyes/clock;
// BtnA holds for setup; BtnB controls moods/settings and holds 3s to restart.
// Hold the AXP192 power button for 1.5s to power off. AP password: 12345678.
// Wi-Fi credentials and timezone are configured in the portal.

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <M5StickC.h>
#include <Eye.h>
#include "time.h"
#include <sys/time.h>
#include <math.h>

enum AlertLevel { ALERT_NONE, ALERT_IMPORTANT, ALERT_CRITICAL };

// ==================================================
// 1. ASSETS & CONFIG
// ==================================================
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 80
TFT_eSprite screen = TFT_eSprite(&M5.Lcd);
#define display screen

// Draw Adafruit-style 1-bit PROGMEM bitmaps on the M5StickC TFT.
void drawMonoBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                    int16_t w, int16_t h, uint16_t color) {
  int16_t byteWidth = (w + 7) / 8;
  for (int16_t yy = 0; yy < h; ++yy) {
    for (int16_t xx = 0; xx < w; ++xx) {
      uint8_t b = pgm_read_byte(bitmap + yy * byteWidth + xx / 8);
      if (b & (0x80 >> (xx & 7))) display.drawPixel(x + xx, y + yy, color);
    }
  }
}

// --- EMOTION PARTICLES (16x16) ---
const unsigned char bmp_heart[] PROGMEM = { 0x00, 0x00, 0x0c, 0x60, 0x1e, 0xf0, 0x3f, 0xf8, 0x7f, 0xfc, 0x7f, 0xfc, 0x7f, 0xfc, 0x3f, 0xf8, 0x1f, 0xf0, 0x0f, 0xe0, 0x07, 0xc0, 0x03, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const unsigned char bmp_zzz[] PROGMEM = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x0c, 0x00, 0x18, 0x00, 0x30, 0x00, 0x7e, 0x00, 0x00, 0x3c, 0x00, 0x0c, 0x00, 0x18, 0x00, 0x30, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00 };
const unsigned char bmp_anger[] PROGMEM = { 0x00, 0x00, 0x11, 0x10, 0x2a, 0x90, 0x44, 0x40, 0x80, 0x20, 0x80, 0x20, 0x44, 0x40, 0x2a, 0x90, 0x11, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// --- GLOBALS ---
enum ScreenPage { PAGE_EYES, PAGE_CLOCK, PAGE_CLOCK_SETTINGS };
ScreenPage currentPage = PAGE_EYES;

// MOODS
#define MOOD_NORMAL 0
#define MOOD_HAPPY 1
#define MOOD_SURPRISED 2
#define MOOD_SLEEPY 3
#define MOOD_ANGRY 4
#define MOOD_SAD 5
#define MOOD_EXCITED 6
#define MOOD_LOVE 7
#define MOOD_SUSPICIOUS 8
int currentMood = MOOD_NORMAL;
unsigned long moodOverrideUntil = 0;
unsigned long nextAutoMoodAt = 0;
const unsigned long MOOD_OVERRIDE_MS = 30000;

String wifiSsid; // Loaded from Preferences
String wifiPass; // Loaded from Preferences
const char* ntpServer = "pool.ntp.org";
String tzString;   // Loaded from Preferences (e.g. "IST-5:30")
bool clockIsSet = false;
bool rtcAvailable = false;
bool restartPending = false;
unsigned long restartAt = 0;
bool powerOffPending = false;
unsigned long powerOffAt = 0;
unsigned long lastPowerButtonEvent = 0;
bool clockSaveError = false;

// Clock setup draft; time is stored in the RTC as UTC and converted only for display.
int clockDraft[6] = { 2026, 1, 1, 12, 0, 0 };
uint8_t clockEditField = 0; // 0..5 date/time, 6 timezone, 7 save, 8 cancel
uint8_t clockConfigMode = 0; // 0=field editor, 1=timezone text, 2=timezone choices
String tzEditBuffer;
uint8_t tzEditCursor = 0;
uint8_t tzEditChoice = 0;
uint8_t tzPresetIndex = 0;
bool tzEditError = false;

// IMU fused orientation and eye offsets, refreshed independently of LCD frames.
bool motionSensorOk = false;
float neutralPitch = 0.0f, neutralRoll = 0.0f;
float eyeMotionX = 0.0f, eyeMotionY = 0.0f;
bool motionCalibrated = false;
unsigned long lastMotionSample = 0;

// Centralized red LED state. Alert levels override Wi-Fi indications.
AlertLevel activeAlert = ALERT_NONE;
bool wifiConnecting = false;
bool wifiIntentionallyDisabled = false;
unsigned long wifiConnectDeadline = 0;
bool ledIsOn = false;
unsigned long ledPatternStarted = 0;
unsigned long ledLastUpdate = 0;

// ==================================================
// 2. ULTRA PRO PHYSICS ENGINE
// ==================================================

Eye leftEye, rightEye;
unsigned long lastSaccade = 0;
unsigned long saccadeInterval = 3000;
float breathVal = 0;

// ==================================================
// 2b. CONFIG PORTAL (Wi-Fi + timezone via local web)
// ==================================================
#define CONFIG_AP_SSID   "Kumaran Buddy"
#define CONFIG_AP_PASS   "12345678"
#define CONFIG_HOLD_MS   3000

Preferences prefs;
WebServer configServer(80);
bool inConfigMode = false;

struct TimeZoneEntry { const char* iana; const char* posix; };
// ESP32 newlib accepts POSIX TZ rules, but this image has no zoneinfo database.
// These explicit mappings cover common IANA zones and their current DST rules.
const TimeZoneEntry supportedZones[] = {
  { "UTC", "UTC0" },
  { "Asia/Kolkata", "IST-5:30" },
  { "Asia/Tokyo", "JST-9" },
  { "Asia/Singapore", "SGT-8" },
  { "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0/2" },
  { "Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "America/New_York", "EST5EDT,M3.2.0/2,M11.1.0/2" },
  { "America/Chicago", "CST6CDT,M3.2.0/2,M11.1.0/2" },
  { "America/Denver", "MST7MDT,M3.2.0/2,M11.1.0/2" },
  { "America/Los_Angeles", "PST8PDT,M3.2.0/2,M11.1.0/2" },
  { "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
  { "IST-5:30", "IST-5:30" } // migrate the firmware's original stored value
};

bool timezoneToPosix(const String& input, String& output) {
  for (size_t i = 0; i < sizeof(supportedZones) / sizeof(supportedZones[0]); ++i) {
    if (input == supportedZones[i].iana) {
      output = supportedZones[i].posix;
      return true;
    }
  }
  // Fixed offset syntax is UTC+HH:MM or UTC-HH:MM (east/west of UTC).
  if (input.length() == 9 && input.startsWith("UTC") &&
      (input[3] == '+' || input[3] == '-') && input[6] == ':' &&
      isDigit(input[4]) && isDigit(input[5]) && isDigit(input[7]) && isDigit(input[8])) {
    int hours = (input[4] - '0') * 10 + input[5] - '0';
    int minutes = (input[7] - '0') * 10 + input[8] - '0';
    if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0)) return false;
    // POSIX uses the opposite sign from the user-facing UTC offset.
    char sign = input[3] == '+' ? '-' : '+';
    char zone[16];
    snprintf(zone, sizeof(zone), "UTC%c%02d:%02d", sign, hours, minutes);
    output = zone;
    return true;
  }
  return false;
}

void applyTimeZone() {
  String posix;
  if (!timezoneToPosix(tzString, posix)) {
    tzString = "Asia/Kolkata";
    timezoneToPosix(tzString, posix);
  }
  setenv("TZ", posix.c_str(), 1);
  tzset();
}

void loadConfig() {
  prefs.begin("deskbuddy", true);
  wifiSsid    = prefs.getString("ssid", "");
  wifiPass    = prefs.getString("pass", "");
  tzString    = prefs.getString("tz", "");
  prefs.end();
  // Remove obsolete location and service-key preferences from prior releases.
  prefs.begin("deskbuddy", false);
  prefs.remove("apikey");
  prefs.remove("city");
  prefs.remove("country");
  prefs.end();
  if (tzString.isEmpty()) tzString = "Asia/Kolkata";
  String validatedTz;
  if (!timezoneToPosix(tzString, validatedTz)) tzString = "Asia/Kolkata";
  applyTimeZone();
}

void saveConfig(const String& s, const String& p, const String& tz) {
  prefs.begin("deskbuddy", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.putString("tz", tz);
  prefs.end();
}

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&#39;"; break;
      default: escaped += value[i]; break;
    }
  }
  return escaped;
}

void handleConfigRoot() {
  prefs.begin("deskbuddy", true);
  String sSsid = prefs.getString("ssid", "");
  String sTz   = prefs.getString("tz", "Asia/Kolkata");
  prefs.end();

  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskBuddy Config</title>
<style>
body{font-family:sans-serif;max-width:420px;margin:30px auto;padding:24px;background:#0c1929;color:#e8f4fc;}
h1{color:#5ba3f5;margin-bottom:8px;}
input{width:100%;padding:10px;margin:6px 0;border:1px solid #2d4a6f;border-radius:6px;box-sizing:border-box;background:#1a2d47;color:#e8f4fc;}
input:focus{outline:none;border-color:#5ba3f5;}
button{width:100%;padding:12px;background:#3498db;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer;margin-top:16px;}
button:hover{background:#2980b9;}
label{display:block;margin-top:14px;color:#8ab4e8;font-size:14px;}
.section{margin-top:20px;padding-top:16px;border-top:1px solid #1e3a5f;}
.section-title{color:#5ba3f5;font-size:13px;margin-bottom:8px;}
</style></head><body>
<h1>DeskBuddy Setup</h1>
<form action="/save" method="POST">
<label>WiFi SSID</label><input name="ssid" placeholder="Your WiFi name" value=")rawliteral";
  html += htmlEscape(sSsid);
  html += R"rawliteral(">
<label>WiFi Password</label><input name="pass" type="password" placeholder="WiFi password">
<div class="section"><div class="section-title">Time</div>
<label>Timezone (IANA preset or UTC offset)</label><input name="tz" placeholder="Asia/Kolkata or UTC-05:00" value=")rawliteral";
  html += htmlEscape(sTz);
  html += R"rawliteral(">
</div>
<button type="submit">Save &amp; Reboot</button>
</form></body></html>)rawliteral";
  configServer.send(200, "text/html", html);
}

void handleConfigSave() {
  if (!configServer.hasArg("ssid") || configServer.arg("ssid").length() == 0) {
    configServer.send(400, "text/plain", "SSID required");
    return;
  }
  String s   = configServer.arg("ssid");
  String p   = configServer.arg("pass");
  // A blank password field means keep the saved password, not erase it.
  if (p.isEmpty()) {
    prefs.begin("deskbuddy", true);
    p = prefs.getString("pass", "");
    prefs.end();
  }
  String tz  = configServer.arg("tz");
  prefs.begin("deskbuddy", true);
  if (tz.isEmpty())  tz  = prefs.getString("tz", "IST-5:30");
  prefs.end();
  String parsedTz;
  if (!timezoneToPosix(tz, parsedTz)) {
    configServer.send(400, "text/plain",
      "Unsupported timezone. Use a supported IANA name or UTC+HH:MM / UTC-HH:MM.");
    return;
  }
  saveConfig(s, p, tz);
  configServer.send(200, "text/html",
    "<html><body style='font-family:sans-serif;background:#0c1929;color:#e8f4fc;padding:40px;'>"
    "<h2 style='color:#5ba3f5'>Saved!</h2><p>Rebooting in 2 seconds...</p></body></html>");
  delay(2000);
  ESP.restart();
}

void startConfigPortal() {
  inConfigMode = true;
  wifiConnecting = false;
  wifiIntentionallyDisabled = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASS);
  configServer.on("/", handleConfigRoot);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.begin();
  display.fillSprite(TFT_BLACK);
  display.setFreeFont(NULL);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("Connect to Wi-Fi:\n");
  display.print(CONFIG_AP_SSID);
  display.print("\nPassword:\n");
  display.print(CONFIG_AP_PASS);
  display.print("\nOpen 192.168.4.1");
  display.pushSprite(0, 0);
}

// ==================================================
// 3. LOGIC & NETWORK
// ==================================================
bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const uint8_t days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (month < 1 || month > 12) return 0;
  return month == 2 && isLeapYear(year) ? 29 : days[month - 1];
}

bool validDateTime(const int fields[6]) {
  return fields[0] >= 2000 && fields[0] <= 2099 &&
         fields[1] >= 1 && fields[1] <= 12 &&
         fields[2] >= 1 && fields[2] <= daysInMonth(fields[0], fields[1]) &&
         fields[3] >= 0 && fields[3] <= 23 &&
         fields[4] >= 0 && fields[4] <= 59 &&
         fields[5] >= 0 && fields[5] <= 59;
}

bool rtcReadUtc(struct tm& utc) {
  if (!rtcAvailable) return false;
  // BM8563 seconds bit 7 is VL (voltage-low); the library masks it away.
  Wire1.beginTransmission(0x51);
  Wire1.write(0x02);
  if (Wire1.endTransmission(false) != 0 || Wire1.requestFrom(0x51, 1) != 1) return false;
  uint8_t rawSeconds = Wire1.read();
  if (rawSeconds & 0x80) return false;

  RTC_TimeTypeDef timeData = {};
  RTC_DateTypeDef dateData = {};
  M5.Rtc.GetTime(&timeData);
  M5.Rtc.GetData(&dateData);
  int fields[6] = { dateData.Year, dateData.Month, dateData.Date,
                    timeData.Hours, timeData.Minutes, timeData.Seconds };
  if (!validDateTime(fields)) return false;
  memset(&utc, 0, sizeof(utc));
  utc.tm_year = fields[0] - 1900;
  utc.tm_mon = fields[1] - 1;
  utc.tm_mday = fields[2];
  utc.tm_hour = fields[3];
  utc.tm_min = fields[4];
  utc.tm_sec = fields[5];
  utc.tm_isdst = 0;
  return true;
}

void writeRtcUtc(time_t utcEpoch) {
  if (!rtcAvailable) return;
  struct tm utc = {};
  gmtime_r(&utcEpoch, &utc);
  RTC_DateTypeDef dateData = {};
  dateData.Year = utc.tm_year + 1900;
  dateData.Month = utc.tm_mon + 1;
  dateData.Date = utc.tm_mday;
  dateData.WeekDay = utc.tm_wday;
  RTC_TimeTypeDef timeData = {};
  timeData.Hours = utc.tm_hour;
  timeData.Minutes = utc.tm_min;
  timeData.Seconds = utc.tm_sec;
  M5.Rtc.SetData(&dateData);
  M5.Rtc.SetTime(&timeData);
}

void loadDraftFromClock() {
  struct tm local = {};
  time_t now = time(NULL);
  if (clockIsSet && localtime_r(&now, &local)) {
    clockDraft[0] = local.tm_year + 1900;
    clockDraft[1] = local.tm_mon + 1;
    clockDraft[2] = local.tm_mday;
    clockDraft[3] = local.tm_hour;
    clockDraft[4] = local.tm_min;
    clockDraft[5] = local.tm_sec;
  } else {
    const int safeDraft[6] = { 2026, 1, 1, 12, 0, 0 };
    memcpy(clockDraft, safeDraft, sizeof(clockDraft));
  }
}

bool saveManualClock() {
  if (!validDateTime(clockDraft)) return false;
  struct tm local = {};
  local.tm_year = clockDraft[0] - 1900;
  local.tm_mon = clockDraft[1] - 1;
  local.tm_mday = clockDraft[2];
  local.tm_hour = clockDraft[3];
  local.tm_min = clockDraft[4];
  local.tm_sec = clockDraft[5];
  local.tm_isdst = -1;
  time_t utcEpoch = mktime(&local);
  if (utcEpoch < 0) return false;
  // Reject local wall times that do not exist during a DST spring-forward gap.
  struct tm roundTrip = {};
  if (!localtime_r(&utcEpoch, &roundTrip) ||
      roundTrip.tm_year != clockDraft[0] - 1900 ||
      roundTrip.tm_mon != clockDraft[1] - 1 || roundTrip.tm_mday != clockDraft[2] ||
      roundTrip.tm_hour != clockDraft[3] || roundTrip.tm_min != clockDraft[4] ||
      roundTrip.tm_sec != clockDraft[5]) return false;

  writeRtcUtc(utcEpoch);
  struct timeval tv = { utcEpoch, 0 };
  settimeofday(&tv, NULL);
  clockIsSet = true;
  return true;
}

void initializeClock() {
  Wire1.beginTransmission(0x51);
  rtcAvailable = Wire1.endTransmission() == 0;
  struct tm utc = {};
  clockIsSet = rtcReadUtc(utc);
  if (clockIsSet) {
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t epoch = mktime(&utc);
    applyTimeZone();
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, NULL);
  }
  loadDraftFromClock();
}

void initializeMotionSensor() {
  motionSensorOk = M5.Imu.Init() == 0;
  if (motionSensorOk) {
    float pitch = 0, roll = 0, yaw = 0;
    M5.Imu.getAhrsData(&pitch, &roll, &yaw);
    neutralPitch = pitch;
    neutralRoll = roll;
    motionCalibrated = true;
    lastMotionSample = millis();
  }
}

void updateMotionSensor(unsigned long now) {
  if (!motionSensorOk || now - lastMotionSample < 40) return;
  float pitch = 0, roll = 0, yaw = 0;
  M5.Imu.getAhrsData(&pitch, &roll, &yaw); // AHRS fuses accelerometer and gyroscope.
  lastMotionSample = now;
  if (!motionCalibrated) {
    neutralPitch = pitch;
    neutralRoll = roll;
    motionCalibrated = true;
  }
  float deltaRoll = roll - neutralRoll;
  float deltaPitch = pitch - neutralPitch;
  if (fabsf(deltaRoll) < 1.0f) deltaRoll = 0;
  if (fabsf(deltaPitch) < 1.0f) deltaPitch = 0;
  float targetX = constrain(deltaRoll * 0.32f, -7.0f, 7.0f);
  float targetY = constrain(deltaPitch * 0.24f, -5.0f, 5.0f);
  eyeMotionX += (targetX - eyeMotionX) * 0.22f;
  eyeMotionY += (targetY - eyeMotionY) * 0.22f;
}

void setAlertLevel(AlertLevel level) {
  if (activeAlert != level) {
    activeAlert = level;
    ledPatternStarted = millis();
  }
}

void initializeStatusLed() {
  pinMode(M5_LED, OUTPUT);
  digitalWrite(M5_LED, HIGH); // Original M5StickC red LED is active-low.
  ledIsOn = false;
  ledPatternStarted = millis();
}

void updateStatusLed(unsigned long now) {
  if (now - ledLastUpdate < 20) return;
  ledLastUpdate = now;
  bool on = false;
  unsigned long elapsed = now - ledPatternStarted;
  if (activeAlert == ALERT_CRITICAL) {
    unsigned long t = elapsed % 1200;
    on = t < 90 || (t >= 180 && t < 270) || (t >= 360 && t < 450);
  } else if (activeAlert == ALERT_IMPORTANT) {
    unsigned long t = elapsed % 1400;
    on = t < 180 || (t >= 360 && t < 540);
  } else if (wifiConnecting) {
    on = elapsed % 400 < 200;
  } else if (wifiIntentionallyDisabled) {
    unsigned long t = elapsed % 1200;
    on = t < 100 || (t >= 220 && t < 320); // AP/setup mode: double pulse.
  } else if (WiFi.status() != WL_CONNECTED) {
    on = elapsed % 1200 < 140;
  }
  if (on != ledIsOn) {
    digitalWrite(M5_LED, on ? LOW : HIGH);
    ledIsOn = on;
  }
}

void enterClockSettings() {
  loadDraftFromClock();
  clockEditField = 0;
  clockConfigMode = 0;
  currentPage = PAGE_CLOCK_SETTINGS;
}

void cycleClockField() {
  clockEditField = (clockEditField + 1) % 9;
}

void adjustClockDraft() {
  clockSaveError = false;
  switch (clockEditField) {
    case 0: clockDraft[0] = clockDraft[0] >= 2099 ? 2000 : clockDraft[0] + 1; break;
    case 1:
      clockDraft[1] = clockDraft[1] >= 12 ? 1 : clockDraft[1] + 1;
      if (clockDraft[2] > daysInMonth(clockDraft[0], clockDraft[1]))
        clockDraft[2] = daysInMonth(clockDraft[0], clockDraft[1]);
      break;
    case 2:
      clockDraft[2] = clockDraft[2] >= daysInMonth(clockDraft[0], clockDraft[1]) ? 1 : clockDraft[2] + 1;
      break;
    case 3: clockDraft[3] = (clockDraft[3] + 1) % 24; break;
    case 4: clockDraft[4] = (clockDraft[4] + 1) % 60; break;
    case 5: clockDraft[5] = (clockDraft[5] + 1) % 60; break;
    default: break;
  }
}

void enterTimezoneEditor() {
  tzPresetIndex = 0;
  for (uint8_t i = 0; i < sizeof(supportedZones) / sizeof(supportedZones[0]) - 1; ++i) {
    if (tzString == supportedZones[i].iana) tzPresetIndex = i;
  }
  clockConfigMode = 2;
}

void enterTimezoneTextEditor() {
  tzEditBuffer = "";
  tzEditCursor = 0;
  tzEditChoice = 0;
  tzEditError = false;
  clockConfigMode = 1;
}

void selectTimezonePreset() {
  const uint8_t presetCount = sizeof(supportedZones) / sizeof(supportedZones[0]) - 1;
  if (tzPresetIndex == presetCount) {
    enterTimezoneTextEditor();
    return;
  }
  if (tzPresetIndex == presetCount + 1) {
    clockConfigMode = 0;
    return;
  }
  tzString = supportedZones[tzPresetIndex].iana;
  prefs.begin("deskbuddy", false);
  prefs.putString("tz", tzString);
  prefs.end();
  applyTimeZone(); // The UTC epoch and RTC contents are unchanged.
  clockConfigMode = 0;
}

void acceptTimezoneCharacter() {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/_+-:";
  const uint8_t charCount = sizeof(alphabet) - 1;
  if (tzEditChoice < charCount) {
    if (tzEditBuffer.length() < 47) {
      tzEditBuffer += alphabet[tzEditChoice];
      ++tzEditCursor;
    }
    String parsed;
    tzEditChoice = tzEditBuffer.length() >= 8 && timezoneToPosix(tzEditBuffer, parsed)
                     ? charCount + 1 : 0;
  } else if (tzEditChoice == charCount) { // DEL
    if (!tzEditBuffer.isEmpty()) {
      tzEditBuffer.remove(tzEditBuffer.length() - 1);
      if (tzEditCursor) --tzEditCursor;
    }
    tzEditChoice = 0;
  } else if (tzEditChoice == charCount + 1) { // DONE
    String posix;
    if (timezoneToPosix(tzEditBuffer, posix)) {
      tzString = tzEditBuffer;
      prefs.begin("deskbuddy", false);
      prefs.putString("tz", tzString);
      prefs.end();
      applyTimeZone(); // UTC epoch/RTC is unchanged.
      clockConfigMode = 0;
      clockEditField = 0;
      tzEditError = false;
    } else { tzEditError = true; tzEditChoice = 0; }
  } else { // CANCEL
    clockConfigMode = 2;
    tzEditError = false;
  }
}

void handleTouch() {
  // BtnA held 3s opens setup. BtnB retains settings/mood and restart gestures.
  // The AXP192 power button is handled separately as the third physical button.
  static bool aWasDown = false, bWasDown = false;
  static unsigned long aDownAt = 0, bDownAt = 0;
  static bool aLongHandled = false, bLongHandled = false, bRepeatAdjusted = false;
  static unsigned long lastButtonRepeat = 0;
  unsigned long now = millis();
  bool aDown = M5.BtnA.isPressed();
  bool bDown = M5.BtnB.isPressed();

  if (aDown && !aWasDown) { aDownAt = now; aLongHandled = false; }
  if (bDown && !bWasDown) {
    bDownAt = now;
    bLongHandled = false;
    bRepeatAdjusted = false;
    lastButtonRepeat = now;
  }
  if (bDown && currentPage == PAGE_CLOCK_SETTINGS && clockConfigMode == 0 && clockEditField < 6 &&
      now - bDownAt >= 500 && now - lastButtonRepeat >= 140) {
    adjustClockDraft();
    bRepeatAdjusted = true;
    lastButtonRepeat = now;
  }
  if (bDown && !bLongHandled && now - bDownAt >= 3000) {
    bLongHandled = true;
    restartPending = true;
    restartAt = now + 600;
  }
  if (aDown && !aLongHandled && now - aDownAt >= CONFIG_HOLD_MS) {
    aLongHandled = true;
    startConfigPortal();
  }

  if (!aDown && aWasDown && !aLongHandled) {
    if (currentPage == PAGE_CLOCK_SETTINGS) {
      if (clockConfigMode == 0) cycleClockField();
      else if (clockConfigMode == 1) {
        static const uint8_t alphabetCount = 26 + 26 + 10 + 5;
        const uint8_t terminalChoices = 3; // DEL, DONE, CANCEL after ordinary characters.
        tzEditChoice = (tzEditChoice + 1) % (alphabetCount + terminalChoices);
      } else {
        const uint8_t presetCount = sizeof(supportedZones) / sizeof(supportedZones[0]) + 1;
        tzPresetIndex = (tzPresetIndex + 1) % presetCount; // custom text and Back follow the zones
      }
    }
  }

  if (!bDown && bWasDown) {
    unsigned long held = now - bDownAt;
    if (!bLongHandled && !bRepeatAdjusted && held >= 800) {
      if (currentPage == PAGE_CLOCK_SETTINGS) {
        if (clockConfigMode == 1) acceptTimezoneCharacter();
        else if (clockConfigMode == 2) selectTimezonePreset();
        else if (clockEditField == 6) enterTimezoneEditor();
        else if (clockEditField == 7) {
          if (saveManualClock()) { currentPage = PAGE_CLOCK; clockSaveError = false; setAlertLevel(ALERT_NONE); }
          else { clockSaveError = true; setAlertLevel(ALERT_IMPORTANT); }
        } else if (clockEditField == 8) { currentPage = PAGE_CLOCK; setAlertLevel(ALERT_NONE); }
      } else if (currentPage == PAGE_EYES) {
        currentMood = (currentMood + 1) % (MOOD_SUSPICIOUS + 1);
        moodOverrideUntil = now + MOOD_OVERRIDE_MS;
        nextAutoMoodAt = moodOverrideUntil;
        lastSaccade = 0;
      }
    } else if (!bLongHandled && !bRepeatAdjusted && currentPage == PAGE_CLOCK_SETTINGS) {
      if (clockConfigMode == 1) acceptTimezoneCharacter();
      else if (clockConfigMode == 2) selectTimezonePreset();
      else if (clockEditField < 6) adjustClockDraft();
      else if (clockEditField == 6) enterTimezoneEditor();
      else if (clockEditField == 7) {
        if (saveManualClock()) { currentPage = PAGE_CLOCK; clockSaveError = false; setAlertLevel(ALERT_NONE); }
        else { clockSaveError = true; setAlertLevel(ALERT_IMPORTANT); }
      } else if (clockEditField == 8) { currentPage = PAGE_CLOCK; setAlertLevel(ALERT_NONE); }
    } else if (!bLongHandled && !bRepeatAdjusted && currentPage == PAGE_CLOCK) {
      enterClockSettings();
    } else if (!bLongHandled && !bRepeatAdjusted && currentPage == PAGE_EYES) {
      currentMood = (currentMood + 1) % (MOOD_SUSPICIOUS + 1);
      moodOverrideUntil = now + MOOD_OVERRIDE_MS;
      nextAutoMoodAt = moodOverrideUntil;
      lastSaccade = 0;
    }
  }
  aWasDown = aDown;
  bWasDown = bDown;
}

// ==================================================
// 4. DRAWING & ANIMATION
// ==================================================

void drawEyelidMask(float x, float y, float w, float h, int mood, bool isLeft) {
  int ix = (int)x;
  int iy = (int)y;
  int iw = (int)w;
  int ih = (int)h;
  display.setTextColor(TFT_BLACK);

  // ANGRY: Sharp slanted cut
  if (mood == MOOD_ANGRY) {
    if (isLeft)
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy + i, ix + iw, iy - 6 + i, TFT_BLACK);
    else
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy - 6 + i, ix + iw, iy + i, TFT_BLACK);
  }
  // SAD: Inverse slanted cut
  else if (mood == MOOD_SAD) {
    if (isLeft)
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy - 6 + i, ix + iw, iy + i, TFT_BLACK);
    else
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy + i, ix + iw, iy - 6 + i, TFT_BLACK);
  }
  // HAPPY/LOVE: Cheek push up
  else if (mood == MOOD_HAPPY || mood == MOOD_LOVE || mood == MOOD_EXCITED) {
    display.fillRect(ix, iy + ih - 12, iw, 14, TFT_BLACK);
    display.fillCircle(ix + iw / 2, iy + ih + 6, iw / 1.3, TFT_BLACK);  // Round cut
  }
  // SLEEPY: Heavy lids
  else if (mood == MOOD_SLEEPY) {
    display.fillRect(ix, iy, iw, ih / 2 + 2, TFT_BLACK);
  }
  // SUSPICIOUS: One eye squint, one open
  else if (mood == MOOD_SUSPICIOUS) {
    if (isLeft) display.fillRect(ix, iy, iw, ih / 2 - 2, TFT_BLACK);
    else display.fillRect(ix, iy + ih - 8, iw, 8, TFT_BLACK);
  }
}

void drawUltraProEye(Eye& e, bool isLeft) {
  int ix = (int)(e.x + eyeMotionX);
  int iy = (int)(e.y + eyeMotionY);
  int iw = (int)e.w;
  int ih = (int)e.h;

  // 1. Draw Sclera (White base)
  int r = 8;
  if (iw < 20) r = 3;
  display.fillRoundRect(ix, iy, iw, ih, r, TFT_WHITE);

  // 2. Draw Pupil (Black inner eye) - Constrained to be inside
  // Calculate center of eye
  int cx = ix + iw / 2;
  int cy = iy + ih / 2;

  // Pupil size is proportional to eye size
  int pw = iw / 2.2;
  int ph = ih / 2.2;

  // Apply pupil offset (Gaze)
  int px = cx + (int)e.pupilX - (pw / 2);
  int py = cy + (int)e.pupilY - (ph / 2);

  // Clamp pupil to ensure it doesn't leave the eye completely
  if (px < ix) px = ix;
  if (px + pw > ix + iw) px = ix + iw - pw;
  if (py < iy) py = iy;
  if (py + ph > iy + ih) py = iy + ih - ph;

  display.fillRoundRect(px, py, pw, ph, r / 2, TFT_BLACK);

  // 3. Draw Specular Highlight (The "Glint" of life)
  // A tiny white dot in the top-right of the pupil
  if (iw > 15 && ih > 15) {
    display.fillCircle(px + pw - 4, py + 4, 2, TFT_WHITE);
  }

  // 4. Apply Eyelid Masks (Expressions)
  drawEyelidMask(e.x + eyeMotionX, e.y + eyeMotionY, e.w, e.h, currentMood, isLeft);
}

void updatePhysicsAndMood() {
  unsigned long now = millis();
  breathVal = sin(now / 800.0) * 1.5;  // Breathing effect

  // --- BLINK LOGIC ---
  if (now > leftEye.nextBlinkTime) {
    leftEye.blinking = true;
    leftEye.lastBlink = now;
    rightEye.blinking = true;
    leftEye.nextBlinkTime = now + random(2000, 6000);
  }
  if (leftEye.blinking) {
    leftEye.targetH = 2;
    rightEye.targetH = 2;  // Close
    if (now - leftEye.lastBlink > 120) {
      leftEye.blinking = false;
      rightEye.blinking = false;
    }
  }

  // --- SACCADE (Gaze) LOGIC ---
  if (!motionSensorOk && !leftEye.blinking && now - lastSaccade > saccadeInterval) {
    lastSaccade = now;
    saccadeInterval = random(500, 3000);

    // Pick direction
    int dir = random(0, 10);
    float lx = 0, ly = 0;

    if (dir < 4) {
      lx = 0;
      ly = 0;
    }  // Center
    else if (dir == 4) {
      lx = -6;
      ly = -4;
    }  // TL
    else if (dir == 5) {
      lx = 6;
      ly = -4;
    }  // TR
    else if (dir == 6) {
      lx = -6;
      ly = 4;
    }  // BL
    else if (dir == 7) {
      lx = 6;
      ly = 4;
    }  // BR
    else if (dir == 8) {
      lx = 8;
      ly = 0;
    }  // R
    else if (dir == 9) {
      lx = -8;
      ly = 0;
    }  // L

    // Move pupil target relative to center
    leftEye.targetPupilX = lx;
    leftEye.targetPupilY = ly;
    rightEye.targetPupilX = lx;
    rightEye.targetPupilY = ly;

    // Move eye container slightly (Head follow)
    leftEye.targetX = 28 + (lx * 0.3);
    leftEye.targetY = 20 + (ly * 0.3);
    rightEye.targetX = 92 + (lx * 0.3);
    rightEye.targetY = 20 + (ly * 0.3);
  }

  // --- MOOD SHAPES (Overrides targets) ---
  if (!leftEye.blinking) {
    float baseW = 40;
    float baseH = 40;

    // Breathing effect applied to height
    baseH += breathVal;

    switch (currentMood) {
      case MOOD_NORMAL:
        leftEye.targetW = baseW;
        leftEye.targetH = baseH;
        rightEye.targetW = baseW;
        rightEye.targetH = baseH;
        break;
      case MOOD_HAPPY:
      case MOOD_LOVE:
        leftEye.targetW = 40;
        leftEye.targetH = 32;
        rightEye.targetW = 40;
        rightEye.targetH = 32;
        break;
      case MOOD_SURPRISED:
        leftEye.targetW = 30;
        leftEye.targetH = 45;
        rightEye.targetW = 30;
        rightEye.targetH = 45;
        // Jitter pupil
        leftEye.targetPupilX += random(-1, 2);
        break;
      case MOOD_SLEEPY:
        leftEye.targetW = 38;
        leftEye.targetH = 30;
        rightEye.targetW = 38;
        rightEye.targetH = 30;
        break;
      case MOOD_ANGRY:
        leftEye.targetW = 34;
        leftEye.targetH = 32;
        rightEye.targetW = 34;
        rightEye.targetH = 32;
        break;
      case MOOD_SAD:
        leftEye.targetW = 34;
        leftEye.targetH = 40;
        rightEye.targetW = 34;
        rightEye.targetH = 40;
        break;
      case MOOD_SUSPICIOUS:
        leftEye.targetW = 36;
        leftEye.targetH = 20;  // Left Squint
        rightEye.targetW = 36;
        rightEye.targetH = 42;  // Right Wide
        break;
    }
  }

  leftEye.update();
  rightEye.update();
}

void drawEmoPage() {
  updatePhysicsAndMood();

  // Draw Floating Particles based on Mood
  if (currentMood == MOOD_LOVE) {
    drawMonoBitmap(70, 2, bmp_heart, 16, 16, TFT_WHITE);
  } else if (currentMood == MOOD_SLEEPY) {
    drawMonoBitmap(136, 2, bmp_zzz, 16, 16, TFT_WHITE);
  } else if (currentMood == MOOD_ANGRY) {
    drawMonoBitmap(70, 2, bmp_anger, 16, 16, TFT_WHITE);
  }

  drawUltraProEye(leftEye, true);
  drawUltraProEye(rightEye, false);
}

// --- STANDARD PAGES ---
void drawClock() {
  if (!clockIsSet) {
    display.setFreeFont(NULL);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(24, 22);
    display.print(rtcAvailable ? "Clock Not Set" : "RTC Unavailable");
    display.setCursor(19, 43);
    display.print("B: Set Clock");
    return;
  }
  struct tm t;
  if (!getLocalTime(&t)) {
    display.setFreeFont(NULL);
    display.setCursor(30, 30);
    display.print("Syncing...");
    return;
  }
  String ampm = (t.tm_hour >= 12) ? "PM" : "AM";
  int h12 = t.tm_hour % 12;
  if (h12 == 0) h12 = 12;
  display.setTextColor(TFT_WHITE);
  display.setFreeFont(NULL);
  display.setTextSize(1);
  display.setCursor(137, 2);
  display.print(ampm);
  display.setFreeFont(&FreeSansBold18pt7b);
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", h12, t.tm_min);
  uint16_t w = display.textWidth(timeStr);
  display.setCursor((SCREEN_WIDTH - w) / 2, 48);
  display.print(timeStr);
  display.setFreeFont(&FreeSans9pt7b);
  char dateStr[20];
  strftime(dateStr, 20, "%a, %b %d", &t);
  w = display.textWidth(dateStr);
  display.setCursor((SCREEN_WIDTH - w) / 2, 76);
  display.print(dateStr);
}
void drawClockSettings() {
  display.setFreeFont(NULL);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  if (clockConfigMode == 2) {
    const uint8_t presetCount = sizeof(supportedZones) / sizeof(supportedZones[0]) - 1;
    display.setCursor(0, 0);
    display.print("TIME ZONE");
    display.setTextColor(TFT_YELLOW, TFT_BLACK);
    display.setCursor(0, 17);
    if (tzPresetIndex < presetCount) display.print(supportedZones[tzPresetIndex].iana);
    else if (tzPresetIndex == presetCount) display.print("Custom text...");
    else display.print("Back to settings");
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(0, 37);
    display.print("Current: ");
    display.print(tzString.substring(0, 20));
    display.setCursor(0, 55);
    display.print("A: Next  B: Select");
    display.setCursor(0, 68);
    display.print("More choices via text");
    return;
  }
  if (clockConfigMode == 1) {
    display.setCursor(0, 0);
    display.print("TIME ZONE ENTRY");
    display.setCursor(0, 13);
    display.print("Text: ");
    uint8_t start = tzEditCursor > 18 ? tzEditCursor - 18 : 0;
    display.print(tzEditBuffer.substring(start, start + 20));
    display.setCursor(0, 25);
    display.print("Candidate:");
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/_+-:";
    uint8_t charCount = sizeof(alphabet) - 1;
    if (tzEditChoice < charCount) display.printf(" %c", alphabet[tzEditChoice]);
    else if (tzEditChoice == charCount) display.print(" DEL");
    else if (tzEditChoice == charCount + 1) display.print(" DONE");
    else display.print(" CANCEL");
    display.setCursor(0, 43);
    display.print("A: Next  B: Add");
    display.setCursor(0, 55);
    display.print("Type zone, then DONE");
    display.setCursor(0, 67);
    display.print(tzEditError ? "Invalid zone or offset" : "IANA / UTC+HH:MM");
    return;
  }

  display.setCursor(0, 0);
  display.print("CLOCK SETUP");
  char dateLine[24], timeLine[24];
  snprintf(dateLine, sizeof(dateLine), "Date %04d-%02d-%02d", clockDraft[0], clockDraft[1], clockDraft[2]);
  snprintf(timeLine, sizeof(timeLine), "Time %02d:%02d:%02d", clockDraft[3], clockDraft[4], clockDraft[5]);
  display.setCursor(0, 13);
  display.print(dateLine);
  display.setCursor(0, 24);
  display.print(timeLine);
  display.setCursor(0, 35);
  display.print("TZ ");
  display.print(tzString.substring(0, 23));

  static const char* fields[] = { "YEAR", "MONTH", "DAY", "HOUR", "MIN", "SEC", "TIME ZONE", "SAVE", "CANCEL" };
  display.fillRect(0, 47, SCREEN_WIDTH, 12, TFT_WHITE);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setCursor(3, 49);
  display.print("Selected: ");
  display.print(fields[clockEditField]);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(0, 63);
  if (clockEditField < 6) display.print("A: Next field  B: +1");
  else display.print("A: Next  B: Select");
  display.setCursor(0, 70);
  if (clockSaveError) display.print("Invalid time / DST gap");
  else display.print("Hold B 3s: Restart");
}

// ==================================================
// 5. BOOT & MAIN
// ==================================================

void handlePowerButton() {
  uint8_t press = M5.Axp.GetBtnPress();
  unsigned long now = millis();
  if (press == 0 || (lastPowerButtonEvent != 0 && now - lastPowerButtonEvent < 250)) return;
  lastPowerButtonEvent = now;
  if (press & 0x01) { // AXP192 long-press event (library documents 1.5 seconds).
    display.fillSprite(TFT_BLACK);
    display.setFreeFont(NULL);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(28, 38);
    display.print("Powering off...");
    display.pushSprite(0, 0);
    powerOffPending = true;
    powerOffAt = now + 700;
  } else if (press & 0x02) { // AXP192 short-press event.
    currentPage = currentPage == PAGE_EYES ? PAGE_CLOCK : PAGE_EYES;
  }
}

void setup() {
  M5.begin();
  initializeStatusLed();
  M5.Lcd.setRotation(1); // Landscape: 160 x 80 pixels
  M5.Lcd.fillScreen(TFT_BLACK);
  screen.setColorDepth(16);
  if (!screen.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT)) {
    // The 160x80x16 framebuffer is small enough for the original ESP32.
    // Keep a direct-render fallback if allocation fails.
    screen.setColorDepth(8);
    screen.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
  }
  if (screen.width() == SCREEN_WIDTH && screen.height() == SCREEN_HEIGHT) {
    screen.fillSprite(TFT_BLACK);
    screen.pushSprite(0, 0);
  }
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Axp.ScreenBreath(12);
  M5.Axp.EnablePressIRQ(true, true);
  randomSeed((uint32_t)esp_random());

  loadConfig();
  initializeClock();
  initializeMotionSensor();

  // Eye positions sized for the 160 x 80 landscape display.
  leftEye.init(28, 20, 40, 40);
  rightEye.init(92, 20, 40, 40);

  if (wifiSsid.isEmpty()) { startConfigPortal(); return; }
  wifiIntentionallyDisabled = false;
  wifiConnecting = true;
  ledPatternStarted = millis();
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  wifiConnectDeadline = millis() + 15000;
  nextAutoMoodAt = millis() + random(12000, 24000);
}

void loop() {
  unsigned long now = millis();
  M5.update();
  handleTouch();
  handlePowerButton();
  if (powerOffPending) {
    if ((long)(now - powerOffAt) >= 0) M5.Axp.PowerOff();
    updateStatusLed(now);
    return;
  }
  if (inConfigMode) {
    configServer.handleClient();
    updateStatusLed(now);
    return;
  }
  if (wifiConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnecting = false;
      ledPatternStarted = now;
      applyTimeZone();
      configTime(0, 0, ntpServer);
    } else if ((long)(now - wifiConnectDeadline) >= 0) {
      startConfigPortal();
      return;
    }
  }
  updateMotionSensor(now);
  updateStatusLed(now);
  if (!clockIsSet && time(NULL) > 1704067200) {
    // A successful NTP sync can recover the clock if the RTC was unavailable.
    clockIsSet = true;
    writeRtcUtc(time(NULL));
    loadDraftFromClock();
  }
  if (restartPending) {
    static bool restartFrameShown = false;
    if (!restartFrameShown) {
      display.fillSprite(TFT_BLACK);
      display.setFreeFont(NULL);
      display.setTextColor(TFT_WHITE, TFT_BLACK);
      display.setCursor(42, 34);
      display.print("Restarting...");
      display.pushSprite(0, 0);
      restartFrameShown = true;
    }
    if ((long)(millis() - restartAt) >= 0) ESP.restart();
    return;
  }
  if (currentPage == PAGE_EYES && (long)(now - nextAutoMoodAt) >= 0 &&
      (long)(now - moodOverrideUntil) >= 0) {
    static const uint8_t moods[] = { MOOD_NORMAL, MOOD_HAPPY, MOOD_SURPRISED,
                                     MOOD_SLEEPY, MOOD_EXCITED, MOOD_SUSPICIOUS };
    int nextMood = moods[random(0, sizeof(moods) / sizeof(moods[0]))];
    if (nextMood == currentMood) nextMood = moods[(random(0, sizeof(moods) / sizeof(moods[0]) - 1) + 1) %
                                                   (sizeof(moods) / sizeof(moods[0]))];
    currentMood = nextMood;
    lastSaccade = 0;
    nextAutoMoodAt = now + random(15000, 35000);
  }

  static unsigned long lastFrame = 0;
  const unsigned long FRAME_INTERVAL = 33; // About 30 fps; limits TFT traffic.
  if (now - lastFrame < FRAME_INTERVAL) return;
  lastFrame = now;

  display.fillSprite(TFT_BLACK);
  switch (currentPage) {
    case PAGE_EYES: drawEmoPage(); break;
    case PAGE_CLOCK: drawClock(); break;
    case PAGE_CLOCK_SETTINGS: drawClockSettings(); break;
  }
  display.pushSprite(0, 0);
}
