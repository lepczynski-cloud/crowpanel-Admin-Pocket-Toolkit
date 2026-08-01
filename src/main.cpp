#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <NimBLEDevice.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

TFT_eSPI tft;

// Elecrow's factory firmware for the 2.8-inch Miner uses TFT_eSPI's built-in
// XPT2046 support. The touch controller shares SCLK/MOSI with the display,
// uses GPIO 4 for MISO and GPIO 33 for chip select. Those pins are configured
// in platformio.ini.
constexpr int16_t SCREEN_W = 320;
constexpr uint8_t BACKLIGHT_PIN = 27;
constexpr bool RUN_TOUCH_CALIBRATION = false;

// Default calibration published in Elecrow's factory source for this model.
// If touch is offset on your unit, set RUN_TOUCH_CALIBRATION to true once,
// upload, follow the corner prompts and copy the values printed to Serial.
uint16_t touchCalibration[5] = {189, 3416, 359, 3439, 1};

// Warm high-contrast palette. The previous blue/white focus state was hard to
// read on the small LCD, so the UI now uses charcoal panels, amber accents and
// off-white text.
constexpr uint16_t COLOR_BG = 0x18C3;
constexpr uint16_t COLOR_PANEL = 0x2945;
constexpr uint16_t COLOR_PANEL_2 = 0x39C7;
constexpr uint16_t COLOR_RAISED = 0x4A49;
constexpr uint16_t COLOR_BORDER = 0x632C;
constexpr uint16_t COLOR_TEXT = 0xEF7D;
constexpr uint16_t COLOR_MUTED = 0xBDF7;
constexpr uint16_t COLOR_ACCENT = 0xFDA0;
constexpr uint16_t COLOR_ACCENT_DARK = 0xAB20;
constexpr uint16_t COLOR_SUCCESS = 0x76E9;
constexpr uint16_t COLOR_WARNING = 0xFE40;
constexpr uint16_t COLOR_DANGER = 0xFBAE;

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

enum class Screen {
  Home,
  Cidr,
  Vlsm,
  Clock,
  Cron,
  CronEdit,
  Capacity,
  BackupRetention,
  Chmod,
  Wireless,
  WifiMonitor,
  Reference,
};

enum class StorageTab {
  Storage,
  Backup,
};

enum class WirelessTab {
  Wifi,
  Ble,
};

enum class ChoiceTarget {
  None,
  CronPreset,
  BackupFrequency,
  BackupScheme,
};

enum class NumberTarget {
  None,
  CidrOctet0,
  CidrOctet1,
  CidrOctet2,
  CidrOctet3,
  CidrPrefix,
  VlsmLan1,
  VlsmLan2,
  VlsmLan3,
  VlsmLan4,
  ClockHhmm,
  CronInterval,
  CronHour,
  CronMinute,
  StorageDaily,
  StorageRetention,
  StorageCompression,
  StorageCopies,
  StorageSpare,
  BackupFull,
  BackupIncremental,
  BackupCopies,
  BackupDaily,
  BackupWeekly,
  BackupMonthly,
  ChmodMode,
};

struct ReferenceItem {
  const char* key;
  const char* description;
};

struct HomeTool {
  const char* title;
  const char* subtitle;
  const char* subtitle2;
  Screen screen;
};

struct BackupScheme {
  const char* label;
  uint8_t daily;
  uint8_t weekly;
  uint8_t monthly;
};

struct VlsmResult {
  uint8_t lan;
  uint16_t hosts;
  uint8_t prefix;
  uint32_t network;
  uint32_t block;
  bool valid;
};

struct WifiRow {
  String ssid;
  String bssid;
  uint8_t bssidBytes[6];
  int32_t rssi;
  int32_t channel;
  wifi_auth_mode_t auth;
};

struct BleRow {
  String name;
  String address;
  int rssi;
};

struct NumberEditorState {
  bool active;
  NumberTarget target;
  char title[28];
  char unit[14];
  uint32_t minValue;
  uint32_t maxValue;
  uint32_t currentValue;
  uint8_t maxDigits;
  bool zeroPad;
  char digits[10];
};

struct ChoiceDialogState {
  bool active;
  ChoiceTarget target;
  char title[28];
};

struct CronSchedule {
  uint64_t masks[5];
  bool domWildcard;
  bool dowWildcard;
};

constexpr ReferenceItem HTTP_ITEMS[] = {
    {"200", "OK"},
    {"201", "Created"},
    {"204", "No Content"},
    {"301", "Moved Permanently"},
    {"302", "Found / Temporary redirect"},
    {"304", "Not Modified"},
    {"400", "Bad Request"},
    {"401", "Unauthorized"},
    {"403", "Forbidden"},
    {"404", "Not Found"},
    {"405", "Method Not Allowed"},
    {"408", "Request Timeout"},
    {"409", "Conflict"},
    {"413", "Payload Too Large"},
    {"415", "Unsupported Media Type"},
    {"422", "Unprocessable Content"},
    {"429", "Too Many Requests"},
    {"500", "Internal Server Error"},
    {"502", "Bad Gateway"},
    {"503", "Service Unavailable"},
    {"504", "Gateway Timeout"},
};

constexpr ReferenceItem PORT_ITEMS[] = {
    {"20/21", "FTP data / control"},
    {"22", "SSH"},
    {"25", "SMTP"},
    {"53", "DNS"},
    {"67/68", "DHCP"},
    {"80", "HTTP"},
    {"110", "POP3"},
    {"123", "NTP"},
    {"143", "IMAP"},
    {"161", "SNMP"},
    {"389", "LDAP"},
    {"443", "HTTPS"},
    {"445", "SMB"},
    {"465", "SMTPS"},
    {"514", "Syslog"},
    {"587", "SMTP submission"},
    {"636", "LDAPS"},
    {"993", "IMAPS"},
    {"995", "POP3S"},
    {"1433", "Microsoft SQL Server"},
    {"1521", "Oracle Database"},
    {"2049", "NFS"},
    {"2375", "Docker API plain"},
    {"3000", "Dev web apps / Grafana"},
    {"3306", "MySQL / MariaDB"},
    {"3389", "RDP"},
    {"5432", "PostgreSQL"},
    {"5672", "AMQP / RabbitMQ"},
    {"6379", "Redis"},
    {"6443", "Kubernetes API"},
    {"8080", "Alternate HTTP"},
    {"8443", "Alternate HTTPS"},
    {"9090", "Prometheus"},
    {"9200", "Elasticsearch"},
    {"27017", "MongoDB"},
};

constexpr HomeTool HOME_TOOLS[] = {
    {"CIDR", "subnet", "", Screen::Cidr},
    {"VLSM", "planner", "", Screen::Vlsm},
    {"CLOCK", "time zones", "", Screen::Clock},
    {"CRON", "generator", "parser", Screen::Cron},
    {"CAPACITY", "storage", "backup", Screen::Capacity},
    {"CHMOD", "permissions", "", Screen::Chmod},
    {"WIRELESS", "wifi", "bluetooth", Screen::Wireless},
    {"LOOKUP", "ports", "http codes", Screen::Reference},
};

constexpr BackupScheme BACKUP_SCHEMES[] = {
    {"7D / 4W / 12M", 7, 4, 12},
    {"14D / 8W / 12M", 14, 8, 12},
    {"GFS 7 / 4 / 12", 7, 4, 12},
    {"CUSTOM", 0, 0, 0},
};

constexpr uint8_t BACKUP_FREQS[] = {1, 2, 4, 24};
constexpr uint8_t CRON_PRESET_COUNT = 7;
constexpr uint8_t ITEMS_PER_REF_PAGE = 5;
constexpr uint8_t ITEMS_PER_WIRELESS_PAGE = 4;
constexpr uint8_t MAX_WIFI_ROWS = 14;
constexpr uint8_t MAX_BLE_ROWS = 12;

constexpr size_t HTTP_COUNT = sizeof(HTTP_ITEMS) / sizeof(HTTP_ITEMS[0]);
constexpr size_t PORT_COUNT = sizeof(PORT_ITEMS) / sizeof(PORT_ITEMS[0]);
constexpr size_t HOME_TOOL_COUNT = sizeof(HOME_TOOLS) / sizeof(HOME_TOOLS[0]);
constexpr size_t BACKUP_SCHEME_COUNT = sizeof(BACKUP_SCHEMES) / sizeof(BACKUP_SCHEMES[0]);
constexpr size_t BACKUP_FREQ_COUNT = sizeof(BACKUP_FREQS) / sizeof(BACKUP_FREQS[0]);

Screen currentScreen = Screen::Home;
Screen cidrReturnScreen = Screen::Home;
bool wasTouched = false;
uint32_t lastTouchAt = 0;

// CIDR and VLSM state.
uint8_t cidrOctets[4] = {192, 168, 1, 10};
uint8_t cidrPrefix = 24;
uint16_t vlsmHosts[4] = {100, 50, 20, 10};
uint8_t vlsmSelected = 0;

// Clock state. The device has no RTC in this project, so it starts at 12:00
// UTC and keeps time with millis() after boot. The internal calendar starts at
// the firmware build date only to keep zone/day rollover consistent. Cron uses
// a schedule description and does not depend on the device's current time.
// Any visible zone can be edited and the other zones follow the same UTC value.
int32_t clockUtcSeconds = 12 * 60 * 60;
int32_t clockUtcDayNumber = 0;
uint32_t lastClockMillis = 0;
int32_t lastClockRenderedMinute = -1;
uint8_t selectedClock = 2;
int8_t customClockOffset = 1;

// Cron state.
uint8_t cronPreset = 0;
uint8_t cronIntervalMinutes = 5;
uint8_t cronHour = 2;
uint8_t cronMinute = 0;
bool cronCustom = false;
String cronCustomFields[5] = {"*/5", "*", "*", "*", "*"};
String cronEditFields[5] = {"*/5", "*", "*", "*", "*"};
uint8_t cronEditSelectedField = 0;
int8_t cronEditInvalidField = -1;
String cronEditError;

// Storage and backup state.
StorageTab storageTab = StorageTab::Storage;
uint16_t storageDailyGb = 10;
uint16_t storageRetentionDays = 30;
uint8_t storageCompressionPct = 50;
uint8_t storageCopies = 2;
uint8_t storageSparePct = 20;

uint16_t backupFullGb = 500;
uint16_t backupIncrementalGb = 50;
uint8_t backupFrequencyIndex = 0;
uint8_t backupSchemeIndex = 0;
uint8_t backupCopies = 2;
uint8_t backupCustomDaily = 7;
uint8_t backupCustomWeekly = 4;
uint8_t backupCustomMonthly = 12;

// Chmod state.
bool chmodBits[3][3] = {
    {true, true, true},
    {true, false, true},
    {true, false, true},
};

// Reference state.
bool referenceShowsHttp = true;
size_t referencePage = 0;

// Wireless state. Radios are off by default and only enabled by touch.
WirelessTab wirelessTab = WirelessTab::Wifi;
bool wifiEnabled = false;
bool bleEnabled = false;
bool wifiChannelView = false;
bool wifiPassiveScan = false;
bool bleActiveScan = true;
uint8_t wifiPage = 0;
uint8_t blePage = 0;
uint16_t wifiTotalFound = 0;
uint16_t bleTotalFound = 0;
uint8_t wifiChannelCount[15] = {0};
int16_t wifiChannelBestRssi[15] = {0};
WifiRow wifiRows[MAX_WIFI_ROWS];
uint8_t wifiCount = 0;
BleRow bleRows[MAX_BLE_ROWS];
uint8_t bleCount = 0;
NimBLEScan* bleScanner = nullptr;
String wirelessStatus = "Select a radio, then scan";

WifiRow monitoredWifi;
bool wifiMonitorHasTarget = false;
bool wifiMonitorFound = false;
int16_t wifiMonitorRssi = -100;
uint32_t lastWifiMonitorAt = 0;
int16_t wifiMonitorHistory[36] = {0};
uint8_t wifiMonitorHistoryCount = 0;

NumberEditorState numberEditor = {false, NumberTarget::None, "", "", 0, 0, 0, 0, false, ""};
ChoiceDialogState choiceDialog = {false, ChoiceTarget::None, ""};

const Rect BACK_BUTTON{6, 5, 54, 27};
const Rect CIDR_PREFIX_MINUS{238, 91, 33, 25};
const Rect CIDR_PREFIX_PLUS{275, 91, 33, 25};
const Rect VLSM_BASE_BUTTON{206, 39, 102, 25};
const Rect CLOCK_SET_BUTTON{12, 174, 92, 26};
const Rect CLOCK_TZ_MINUS{112, 174, 45, 26};
const Rect CLOCK_TZ_PLUS{164, 174, 45, 26};
const Rect CLOCK_H_MINUS{12, 207, 68, 27};
const Rect CLOCK_H_PLUS{88, 207, 68, 27};
const Rect CLOCK_M_MINUS{164, 207, 68, 27};
const Rect CLOCK_M_PLUS{240, 207, 68, 27};
const Rect CRON_PRESET_BUTTON{12, 122, 296, 30};
const Rect STORAGE_TAB_STORAGE{12, 38, 143, 26};
const Rect STORAGE_TAB_BACKUP{165, 38, 143, 26};
const Rect REF_HTTP_TAB{12, 39, 143, 30};
const Rect REF_PORT_TAB{165, 39, 143, 30};
const Rect REF_PREV{12, 207, 88, 28};
const Rect REF_NEXT{220, 207, 88, 28};
const Rect WIRELESS_WIFI_TOGGLE{12, 39, 143, 28};
const Rect WIRELESS_BLE_TOGGLE{165, 39, 143, 28};
const Rect WIRELESS_SCAN{12, 72, 92, 26};
const Rect WIRELESS_MODE{114, 72, 92, 26};
const Rect WIRELESS_VIEW{216, 72, 92, 26};
const Rect WIRELESS_PREV{12, 207, 88, 28};
const Rect WIRELESS_NEXT{220, 207, 88, 28};
const Rect WIFI_MONITOR_RESCAN{12, 204, 144, 30};
const Rect WIFI_MONITOR_LIST{164, 204, 144, 30};
const Rect RETENTION_APPLY{12, 198, 296, 34};

void drawCurrentScreen();
void startNumberEditor(NumberTarget target, const char* title, uint32_t value, uint32_t minValue,
                       uint32_t maxValue, const char* unit, uint8_t maxDigits, bool zeroPad = false);
void applyChmodPreset(uint16_t mode);
void setSelectedClockFromHhmm(uint32_t rawValue);
void updateClockRows(bool force);
const char* cronFieldLabel(uint8_t field);
void initializeClockCalendarFromBuildDate();

bool contains(const Rect& rect, uint16_t x, uint16_t y) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

int32_t clampInt32(int32_t value, int32_t minValue, int32_t maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

void drawRoundedPanel(const Rect& rect, uint16_t fill, uint16_t border = COLOR_BORDER, uint8_t radius = 7) {
  tft.fillRoundRect(rect.x, rect.y, rect.w, rect.h, radius, fill);
  tft.drawRoundRect(rect.x, rect.y, rect.w, rect.h, radius, border);
}

void drawButton(const Rect& rect, const char* label, uint16_t fill, uint16_t textColor = COLOR_TEXT,
                uint8_t font = 2, uint16_t border = COLOR_BORDER) {
  drawRoundedPanel(rect, fill, border);
  tft.setTextColor(textColor, fill);
  tft.setTextFont(font);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, rect.x + rect.w / 2, rect.y + rect.h / 2 + 1);
}

void drawHeader(const char* title, bool withBack = true, const char* backLabel = "HOME") {
  tft.fillRect(0, 0, SCREEN_W, 36, COLOR_PANEL);
  tft.drawFastHLine(0, 35, SCREEN_W, COLOR_BORDER);
  if (withBack) {
    drawButton(BACK_BUTTON, backLabel, COLOR_RAISED, COLOR_TEXT, 1);
  }
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(title, withBack ? 190 : SCREEN_W / 2, 18);
}

void drawFooter(const char* text) {
  tft.fillRect(0, 222, SCREEN_W, 18, COLOR_PANEL);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(text, SCREEN_W / 2, 231);
}

String trimForDisplay(const String& value, uint8_t maxLen) {
  if (value.length() <= maxLen) {
    return value;
  }
  return value.substring(0, maxLen - 1) + "~";
}

String ipv4FromUint(uint32_t value) {
  char buffer[20];
  std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", static_cast<unsigned>((value >> 24) & 0xFF),
                static_cast<unsigned>((value >> 16) & 0xFF),
                static_cast<unsigned>((value >> 8) & 0xFF), static_cast<unsigned>(value & 0xFF));
  return String(buffer);
}

uint32_t ipv4ToUint(const uint8_t octets[4]) {
  return (static_cast<uint32_t>(octets[0]) << 24) | (static_cast<uint32_t>(octets[1]) << 16) |
         (static_cast<uint32_t>(octets[2]) << 8) | static_cast<uint32_t>(octets[3]);
}

uint32_t maskForPrefix(uint8_t prefix) {
  if (prefix == 0) {
    return 0U;
  }
  if (prefix >= 32) {
    return 0xFFFFFFFFU;
  }
  return 0xFFFFFFFFU << (32U - prefix);
}

uint64_t addressCountForPrefix(uint8_t prefix) {
  if (prefix >= 32) {
    return 1ULL;
  }
  return 1ULL << (32U - prefix);
}

String formatNumber(uint64_t value) {
  char raw[24];
  std::snprintf(raw, sizeof(raw), "%llu", static_cast<unsigned long long>(value));
  String source(raw);
  String result;
  const int length = source.length();
  for (int i = 0; i < length; ++i) {
    if (i > 0 && (length - i) % 3 == 0) {
      result += ',';
    }
    result += source[i];
  }
  return result;
}

String formatSizeGb(double gb) {
  char buffer[32];
  if (gb >= 1024.0) {
    std::snprintf(buffer, sizeof(buffer), "%.2f TB", gb / 1024.0);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.1f GB", gb);
  }
  return String(buffer);
}

String formatHhmm(uint8_t hour, uint8_t minute) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%02u:%02u", static_cast<unsigned>(hour), static_cast<unsigned>(minute));
  return String(buffer);
}

String formatHhmmRaw(uint32_t value) {
  const uint8_t hour = static_cast<uint8_t>((value / 100U) % 100U);
  const uint8_t minute = static_cast<uint8_t>(value % 100U);
  return formatHhmm(hour, minute);
}

void drawResultCell(const Rect& rect, const char* label, const String& value) {
  drawRoundedPanel(rect, COLOR_PANEL);
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.drawString(label, rect.x + 7, rect.y + 4);
  tft.setTextDatum(BL_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawString(value, rect.x + 7, rect.y + rect.h - 5);
}

uint8_t chmodValueForColumn(uint8_t column) {
  return (chmodBits[column][0] ? 4 : 0) + (chmodBits[column][1] ? 2 : 0) +
         (chmodBits[column][2] ? 1 : 0);
}

String chmodSymbolic() {
  String result;
  const char symbols[3] = {'r', 'w', 'x'};
  for (uint8_t col = 0; col < 3; ++col) {
    for (uint8_t row = 0; row < 3; ++row) {
      result += chmodBits[col][row] ? symbols[row] : '-';
    }
  }
  return result;
}

void applyChmodPreset(uint16_t mode) {
  uint8_t values[3] = {static_cast<uint8_t>((mode / 100) % 10),
                       static_cast<uint8_t>((mode / 10) % 10),
                       static_cast<uint8_t>(mode % 10)};
  for (uint8_t col = 0; col < 3; ++col) {
    if (values[col] > 7) {
      values[col] = 7;
    }
    chmodBits[col][0] = (values[col] & 4U) != 0;
    chmodBits[col][1] = (values[col] & 2U) != 0;
    chmodBits[col][2] = (values[col] & 1U) != 0;
  }
}

uint8_t prefixForHostCount(uint16_t hosts) {
  uint32_t needed = static_cast<uint32_t>(hosts) + 2U;
  uint32_t block = 4;
  uint8_t hostBits = 2;
  while (block < needed && hostBits < 31) {
    block <<= 1U;
    ++hostBits;
  }
  return 32U - hostBits;
}

uint32_t blockForPrefix(uint8_t prefix) {
  if (prefix >= 32) {
    return 1U;
  }
  return 1UL << (32U - prefix);
}

uint16_t stepForHostValue(uint16_t hosts) {
  if (hosts < 50) {
    return 5;
  }
  if (hosts < 200) {
    return 10;
  }
  if (hosts < 1000) {
    return 50;
  }
  return 100;
}

void calculateVlsm(VlsmResult byLan[4], bool& overflow) {
  uint8_t order[4] = {0, 1, 2, 3};
  for (uint8_t i = 0; i < 4; ++i) {
    for (uint8_t j = i + 1; j < 4; ++j) {
      if (vlsmHosts[order[j]] > vlsmHosts[order[i]]) {
        const uint8_t tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
    }
  }

  const uint32_t baseNetwork = ipv4ToUint(cidrOctets) & maskForPrefix(cidrPrefix);
  const uint64_t baseSize = addressCountForPrefix(cidrPrefix);
  uint64_t cursor = baseNetwork;
  const uint64_t end = static_cast<uint64_t>(baseNetwork) + baseSize;
  overflow = false;

  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t lan = order[i];
    const uint8_t prefix = prefixForHostCount(vlsmHosts[lan]);
    const uint32_t block = blockForPrefix(prefix);
    VlsmResult result = {lan, vlsmHosts[lan], prefix, static_cast<uint32_t>(cursor), block, true};
    if (cursor + block > end) {
      result.valid = false;
      overflow = true;
    }
    byLan[lan] = result;
    cursor += block;
  }
}

int32_t daysFromCivil(int32_t year, uint8_t month, uint8_t day) {
  year -= month <= 2;
  const int32_t era = (year >= 0 ? year : year - 399) / 400;
  const uint32_t yearOfEra = static_cast<uint32_t>(year - era * 400);
  const uint32_t shiftedMonth = static_cast<uint32_t>(month + (month > 2 ? -3 : 9));
  const uint32_t dayOfYear = (153U * shiftedMonth + 2U) / 5U + day - 1U;
  const uint32_t dayOfEra = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
  return era * 146097 + static_cast<int32_t>(dayOfEra) - 719468;
}

int32_t floorDayFromSeconds(int64_t totalSeconds) {
  if (totalSeconds >= 0) {
    return static_cast<int32_t>(totalSeconds / 86400LL);
  }
  return static_cast<int32_t>(-((-totalSeconds + 86399LL) / 86400LL));
}

int32_t secondsWithinDay(int64_t totalSeconds, int32_t dayNumber) {
  return static_cast<int32_t>(totalSeconds - static_cast<int64_t>(dayNumber) * 86400LL);
}

void initializeClockCalendarFromBuildDate() {
  const char* buildDate = __DATE__;
  const char* monthNames = "JanFebMarAprMayJunJulAugSepOctNovDec";
  uint8_t month = 1;
  for (uint8_t i = 0; i < 12; ++i) {
    if (std::strncmp(buildDate, monthNames + i * 3, 3) == 0) {
      month = static_cast<uint8_t>(i + 1);
      break;
    }
  }
  const uint8_t day = static_cast<uint8_t>((buildDate[4] == ' ' ? 0 : buildDate[4] - '0') * 10 +
                                           (buildDate[5] - '0'));
  const int32_t year = static_cast<int32_t>(std::strtol(buildDate + 7, nullptr, 10));
  clockUtcDayNumber = daysFromCivil(year, month, day);
}

void localClockParts(int8_t offset, int32_t& dayNumber, uint8_t& hour, uint8_t& minute) {
  const int64_t totalSeconds = static_cast<int64_t>(clockUtcDayNumber) * 86400LL + clockUtcSeconds +
                               static_cast<int32_t>(offset) * 3600;
  dayNumber = floorDayFromSeconds(totalSeconds);
  const int32_t localSeconds = secondsWithinDay(totalSeconds, dayNumber);
  hour = static_cast<uint8_t>(localSeconds / 3600);
  minute = static_cast<uint8_t>((localSeconds / 60) % 60);
}

void syncClock() {
  const uint32_t now = millis();
  if (lastClockMillis == 0) {
    lastClockMillis = now;
    return;
  }
  const uint32_t elapsedMs = now - lastClockMillis;
  const uint32_t elapsedSeconds = elapsedMs / 1000U;
  if (elapsedSeconds == 0) {
    return;
  }
  const int64_t total = static_cast<int64_t>(clockUtcSeconds) + elapsedSeconds;
  clockUtcDayNumber += static_cast<int32_t>(total / 86400LL);
  clockUtcSeconds = static_cast<int32_t>(total % 86400LL);
  lastClockMillis += elapsedSeconds * 1000U;
}

int8_t offsetForClock(uint8_t index) {
  if (index == 0) {
    return 2;
  }
  if (index == 1) {
    return 8;
  }
  if (index == 2) {
    return 0;
  }
  return customClockOffset;
}

const char* nameForClock(uint8_t index) {
  if (index == 0) {
    return "CEST";
  }
  if (index == 1) {
    return "HKT";
  }
  if (index == 2) {
    return "ZULU";
  }
  return "CUSTOM";
}

int32_t secondsForOffset(int8_t offset) {
  int32_t seconds = clockUtcSeconds + static_cast<int32_t>(offset) * 3600;
  while (seconds < 0) {
    seconds += 86400;
  }
  seconds %= 86400;
  return seconds;
}

String formatTimeForOffset(int8_t offset) {
  const int32_t seconds = secondsForOffset(offset);
  return formatHhmm(static_cast<uint8_t>(seconds / 3600), static_cast<uint8_t>((seconds / 60) % 60));
}

uint32_t selectedClockHhmmValue() {
  const int32_t seconds = secondsForOffset(offsetForClock(selectedClock));
  return static_cast<uint32_t>((seconds / 3600) * 100 + ((seconds / 60) % 60));
}

void normalizeClockUtc() {
  while (clockUtcSeconds < 0) {
    clockUtcSeconds += 86400;
    --clockUtcDayNumber;
  }
  while (clockUtcSeconds >= 86400) {
    clockUtcSeconds -= 86400;
    ++clockUtcDayNumber;
  }
  lastClockMillis = millis();
  lastClockRenderedMinute = -1;
}

void adjustSelectedClock(int32_t deltaSeconds) {
  clockUtcSeconds += deltaSeconds;
  normalizeClockUtc();
}

void setSelectedClockFromHhmm(uint32_t rawValue) {
  uint8_t hour = static_cast<uint8_t>((rawValue / 100U) % 100U);
  uint8_t minute = static_cast<uint8_t>(rawValue % 100U);
  if (hour > 23) {
    hour = 23;
  }
  if (minute > 59) {
    minute = 59;
  }

  int32_t localDay = 0;
  uint8_t currentHour = 0;
  uint8_t currentMinute = 0;
  const int8_t offset = offsetForClock(selectedClock);
  localClockParts(offset, localDay, currentHour, currentMinute);
  const int64_t localSeconds = static_cast<int64_t>(localDay) * 86400LL +
                               static_cast<int32_t>(hour) * 3600 + static_cast<int32_t>(minute) * 60;
  const int64_t utcSeconds = localSeconds - static_cast<int32_t>(offset) * 3600;
  clockUtcDayNumber = floorDayFromSeconds(utcSeconds);
  clockUtcSeconds = secondsWithinDay(utcSeconds, clockUtcDayNumber);
  normalizeClockUtc();
}

String wifiAuthName(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN:
      return "OPEN";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI";
    default:
      return "SEC";
  }
}

uint8_t signalPercent(int rssi) {
  if (rssi >= -45) {
    return 100;
  }
  if (rssi <= -90) {
    return 5;
  }
  return static_cast<uint8_t>((rssi + 90) * 100 / 45);
}

uint8_t signalBars(int rssi) {
  const uint8_t percent = signalPercent(rssi);
  if (percent >= 80) {
    return 4;
  }
  if (percent >= 60) {
    return 3;
  }
  if (percent >= 35) {
    return 2;
  }
  return 1;
}

const char* signalLabel(int rssi) {
  if (rssi >= -50) {
    return "Great";
  }
  if (rssi >= -62) {
    return "Good";
  }
  if (rssi >= -72) {
    return "Fair";
  }
  return "Weak";
}

uint16_t signalColor(int rssi) {
  if (rssi >= -62) {
    return COLOR_SUCCESS;
  }
  if (rssi >= -72) {
    return COLOR_WARNING;
  }
  return COLOR_DANGER;
}

void drawSignalBars(int16_t x, int16_t y, int rssi, uint16_t bg) {
  const uint8_t bars = signalBars(rssi);
  const uint16_t color = signalColor(rssi);
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t h = 4 + i * 3;
    const int16_t bx = x + i * 6;
    const int16_t by = y + 14 - h;
    tft.drawRect(bx, by, 4, h, COLOR_MUTED);
    if (i < bars) {
      tft.fillRect(bx + 1, by + 1, 2, h - 2, color);
    } else {
      tft.fillRect(bx + 1, by + 1, 2, h - 2, bg);
    }
  }
}

void drawHomeCard(uint8_t index, const HomeTool& tool) {
  const uint8_t col = index % 4;
  const uint8_t row = index / 4;
  Rect card{static_cast<int16_t>(8 + col * 78), static_cast<int16_t>(44 + row * 82), 72, 72};
  drawRoundedPanel(card, COLOR_PANEL, COLOR_BORDER, 9);
  tft.setTextColor(COLOR_ACCENT, COLOR_PANEL);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(tool.title, card.x + card.w / 2, card.y + 23);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.setTextFont(1);
  if (tool.subtitle2 != nullptr && std::strlen(tool.subtitle2) > 0) {
    tft.drawString(tool.subtitle, card.x + card.w / 2, card.y + 47);
    tft.drawString(tool.subtitle2, card.x + card.w / 2, card.y + 59);
  } else {
    tft.drawString(tool.subtitle, card.x + card.w / 2, card.y + 52);
  }
}

void drawHome() {
  tft.fillScreen(COLOR_BG);
  drawHeader("ADMIN POCKET TOOLKIT", false);
  for (uint8_t i = 0; i < HOME_TOOL_COUNT; ++i) {
    drawHomeCard(i, HOME_TOOLS[i]);
  }
  drawFooter("All tools on one screen | radios off by default");
}

void drawCidr() {
  tft.fillScreen(COLOR_BG);
  drawHeader("CIDR CALCULATOR", true, cidrReturnScreen == Screen::Vlsm ? "VLSM" : "HOME");

  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("Tap any value to type it", 12, 40);
  tft.drawString("PREFIX", 246, 45);

  for (uint8_t i = 0; i < 4; ++i) {
    Rect box{static_cast<int16_t>(12 + i * 53), 56, 45, 31};
    char label[5];
    std::snprintf(label, sizeof(label), "%u", cidrOctets[i]);
    drawButton(box, label, COLOR_PANEL, COLOR_TEXT, 2);
    if (i < 3) {
      tft.setTextColor(COLOR_MUTED, COLOR_BG);
      tft.setTextFont(2);
      tft.setTextDatum(MC_DATUM);
      tft.drawString(".", box.x + box.w + 4, box.y + 16);
    }
  }

  Rect prefixBox{238, 56, 70, 31};
  char prefixLabel[8];
  std::snprintf(prefixLabel, sizeof(prefixLabel), "/%u", cidrPrefix);
  drawButton(prefixBox, prefixLabel, COLOR_ACCENT_DARK, COLOR_TEXT, 2, COLOR_ACCENT);
  drawButton(CIDR_PREFIX_MINUS, "-", COLOR_RAISED, COLOR_TEXT, 2);
  drawButton(CIDR_PREFIX_PLUS, "+", COLOR_RAISED, COLOR_TEXT, 2);

  const uint32_t ip = ipv4ToUint(cidrOctets);
  const uint32_t mask = maskForPrefix(cidrPrefix);
  const uint32_t wildcard = ~mask;
  const uint32_t network = ip & mask;
  const uint32_t broadcast = network | wildcard;
  const uint64_t addresses = addressCountForPrefix(cidrPrefix);
  uint64_t usable = addresses;
  String firstHost = ipv4FromUint(network);
  String lastHost = ipv4FromUint(broadcast);
  if (cidrPrefix <= 30) {
    usable = addresses - 2;
    firstHost = ipv4FromUint(network + 1);
    lastHost = ipv4FromUint(broadcast - 1);
  } else if (cidrPrefix == 31) {
    usable = 2;
  } else {
    usable = 1;
    firstHost = ipv4FromUint(ip);
    lastHost = ipv4FromUint(ip);
  }

  drawResultCell({12, 121, 145, 28}, "Network", ipv4FromUint(network));
  drawResultCell({163, 121, 145, 28}, "Broadcast", ipv4FromUint(broadcast));
  drawResultCell({12, 153, 145, 28}, "First host", firstHost);
  drawResultCell({163, 153, 145, 28}, "Last host", lastHost);
  drawResultCell({12, 185, 145, 28}, "Mask", ipv4FromUint(mask));
  drawResultCell({163, 185, 145, 28}, "Usable / total", formatNumber(usable) + " / " + formatNumber(addresses));

  drawFooter("Prefix also feeds the VLSM planner");
}

void drawVlsmRow(uint8_t lan, const VlsmResult& result) {
  const int16_t y = 70 + static_cast<int16_t>(lan) * 32;
  const bool selected = lan == vlsmSelected;
  const uint16_t border = selected ? COLOR_ACCENT : COLOR_BORDER;
  drawButton({12, y, 28, 28}, "-", COLOR_RAISED, COLOR_TEXT, 2, border);

  Rect valueBox{45, y, 160, 28};
  drawRoundedPanel(valueBox, selected ? COLOR_ACCENT_DARK : COLOR_PANEL, border, 6);
  char center[32];
  std::snprintf(center, sizeof(center), "LAN%u  %u hosts", static_cast<unsigned>(lan + 1),
                static_cast<unsigned>(vlsmHosts[lan]));
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT, selected ? COLOR_ACCENT_DARK : COLOR_PANEL);
  tft.drawString(center, valueBox.x + valueBox.w / 2, valueBox.y + valueBox.h / 2 + 1);

  drawButton({210, y, 28, 28}, "+", COLOR_RAISED, COLOR_TEXT, 2, border);

  Rect prefixBox{244, y, 64, 28};
  const uint16_t prefixBg = result.valid ? COLOR_PANEL : COLOR_DANGER;
  drawRoundedPanel(prefixBox, prefixBg, border, 6);
  char prefix[16];
  std::snprintf(prefix, sizeof(prefix), result.valid ? "/%u" : "NO", static_cast<unsigned>(result.prefix));
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(result.valid ? COLOR_ACCENT : COLOR_TEXT, prefixBg);
  tft.drawString(prefix, prefixBox.x + prefixBox.w / 2, prefixBox.y + prefixBox.h / 2 + 1);
}

void drawVlsm() {
  tft.fillScreen(COLOR_BG);
  drawHeader("VLSM PLANNER");

  const uint32_t baseNetwork = ipv4ToUint(cidrOctets) & maskForPrefix(cidrPrefix);
  char baseLabel[44];
  std::snprintf(baseLabel, sizeof(baseLabel), "Base %s/%u", ipv4FromUint(baseNetwork).c_str(),
                static_cast<unsigned>(cidrPrefix));
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString(baseLabel, 12, 52);
  drawButton(VLSM_BASE_BUTTON, "EDIT CIDR", COLOR_RAISED, COLOR_TEXT, 1);

  VlsmResult results[4];
  bool overflow = false;
  calculateVlsm(results, overflow);

  for (uint8_t lan = 0; lan < 4; ++lan) {
    drawVlsmRow(lan, results[lan]);
  }

  Rect summary{12, 199, 296, 35};
  drawRoundedPanel(summary, overflow ? COLOR_DANGER : COLOR_PANEL);
  const VlsmResult& selected = results[vlsmSelected];
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  if (selected.valid) {
    char line1[70];
    std::snprintf(line1, sizeof(line1), "LAN%u -> %s/%u", static_cast<unsigned>(selected.lan + 1),
                  ipv4FromUint(selected.network).c_str(), static_cast<unsigned>(selected.prefix));
    tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
    tft.drawString(line1, 20, 205);
    tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft.drawString("Tap middle value to type exact host count", 20, 219);
  } else {
    tft.setTextColor(COLOR_TEXT, COLOR_DANGER);
    tft.drawString("Requested subnets exceed the base CIDR", 20, 211);
  }
}

void updateClockRows(bool force) {
  syncClock();
  const int32_t minuteKey = clockUtcSeconds / 60;
  if (!force && minuteKey == lastClockRenderedMinute) {
    return;
  }
  lastClockRenderedMinute = minuteKey;

  for (uint8_t i = 0; i < 4; ++i) {
    Rect row{12, static_cast<int16_t>(54 + i * 29), 296, 27};
    const bool selected = i == selectedClock;
    const uint16_t bg = selected ? COLOR_ACCENT_DARK : COLOR_PANEL;
    drawRoundedPanel(row, bg, selected ? COLOR_ACCENT : COLOR_BORDER, 6);

    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_TEXT, bg);
    tft.drawString(nameForClock(i), row.x + 8, row.y + 14);

    char offsetLabel[14];
    std::snprintf(offsetLabel, sizeof(offsetLabel), "UTC%+d", static_cast<int>(offsetForClock(i)));
    tft.setTextFont(1);
    tft.setTextColor(COLOR_MUTED, bg);
    tft.drawString(offsetLabel, row.x + 76, row.y + 14);

    tft.setTextFont(4);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COLOR_TEXT, bg);
    tft.drawString(formatTimeForOffset(offsetForClock(i)), row.x + row.w - 10, row.y + 14);
  }
}

void drawClock() {
  tft.fillScreen(COLOR_BG);
  drawHeader("WORLD CLOCK");

  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("Tap zone. SET opens HHMM keypad. No NTP.", 12, 40);

  lastClockRenderedMinute = -1;
  updateClockRows(true);

  drawButton(CLOCK_SET_BUTTON, "SET HHMM", COLOR_RAISED, COLOR_TEXT, 1);
  drawButton(CLOCK_TZ_MINUS, "TZ-", selectedClock == 3 ? COLOR_RAISED : COLOR_PANEL,
             selectedClock == 3 ? COLOR_TEXT : COLOR_MUTED, 1);
  drawButton(CLOCK_TZ_PLUS, "TZ+", selectedClock == 3 ? COLOR_RAISED : COLOR_PANEL,
             selectedClock == 3 ? COLOR_TEXT : COLOR_MUTED, 1);
  char selectedLabel[28];
  std::snprintf(selectedLabel, sizeof(selectedLabel), "Editing: %s", nameForClock(selectedClock));
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.drawString(selectedLabel, 220, 187);

  drawButton(CLOCK_H_MINUS, "H -", COLOR_RAISED, COLOR_TEXT, 2);
  drawButton(CLOCK_H_PLUS, "H +", COLOR_RAISED, COLOR_TEXT, 2);
  drawButton(CLOCK_M_MINUS, "M -", COLOR_RAISED, COLOR_TEXT, 2);
  drawButton(CLOCK_M_PLUS, "M +", COLOR_RAISED, COLOR_TEXT, 2);
}

const char* cronPresetNameFor(uint8_t preset) {
  switch (preset) {
    case 0:
      return "Every N minutes";
    case 1:
      return "Hourly";
    case 2:
      return "Daily";
    case 3:
      return "Weekdays";
    case 4:
      return "Weekly Sunday";
    case 5:
      return "Monthly 1st";
    default:
      return "Every 6 hours";
  }
}

const char* cronPresetDetail(uint8_t preset) {
  switch (preset) {
    case 0:
      return "*/N * * * *";
    case 1:
      return "M * * * *";
    case 2:
      return "M H * * *";
    case 3:
      return "M H * * 1-5";
    case 4:
      return "M H * * 0";
    case 5:
      return "M H 1 * *";
    default:
      return "0 */6 * * *";
  }
}

const char* cronPresetName() {
  return cronCustom ? "Custom expression" : cronPresetNameFor(cronPreset);
}

void generatedCronFields(String fields[5]) {
  switch (cronPreset) {
    case 0:
      fields[0] = "*/" + String(cronIntervalMinutes);
      fields[1] = "*";
      fields[2] = "*";
      fields[3] = "*";
      fields[4] = "*";
      break;
    case 1:
      fields[0] = String(cronMinute);
      fields[1] = "*";
      fields[2] = "*";
      fields[3] = "*";
      fields[4] = "*";
      break;
    case 2:
      fields[0] = String(cronMinute);
      fields[1] = String(cronHour);
      fields[2] = "*";
      fields[3] = "*";
      fields[4] = "*";
      break;
    case 3:
      fields[0] = String(cronMinute);
      fields[1] = String(cronHour);
      fields[2] = "*";
      fields[3] = "*";
      fields[4] = "1-5";
      break;
    case 4:
      fields[0] = String(cronMinute);
      fields[1] = String(cronHour);
      fields[2] = "*";
      fields[3] = "*";
      fields[4] = "0";
      break;
    case 5:
      fields[0] = String(cronMinute);
      fields[1] = String(cronHour);
      fields[2] = "1";
      fields[3] = "*";
      fields[4] = "*";
      break;
    default:
      fields[0] = "0";
      fields[1] = "*/6";
      fields[2] = "*";
      fields[3] = "*";
      fields[4] = "*";
      break;
  }
}

void currentCronFields(String fields[5]) {
  if (cronCustom) {
    for (uint8_t i = 0; i < 5; ++i) {
      fields[i] = cronCustomFields[i];
    }
    return;
  }
  generatedCronFields(fields);
}

bool parseCronUnsigned(const String& text, uint16_t& value) {
  if (text.length() == 0) {
    return false;
  }
  uint32_t parsed = 0;
  for (uint16_t i = 0; i < text.length(); ++i) {
    const char character = text[i];
    if (character < '0' || character > '9') {
      return false;
    }
    parsed = parsed * 10U + static_cast<uint32_t>(character - '0');
    if (parsed > 999U) {
      return false;
    }
  }
  value = static_cast<uint16_t>(parsed);
  return true;
}

String cronFieldError(uint8_t field, const char* detail) {
  return String(cronFieldLabel(field)) + ": " + detail;
}

bool parseCronField(const String& text, uint8_t field, uint64_t& mask, bool& wildcard, String& error) {
  static const uint8_t minimums[5] = {0, 0, 1, 1, 0};
  static const uint8_t maximums[5] = {59, 23, 31, 12, 7};
  const uint8_t minimum = minimums[field];
  const uint8_t maximum = maximums[field];
  const uint8_t maximumStep = static_cast<uint8_t>(maximum - minimum + 1);

  mask = 0;
  wildcard = text == "*";
  if (text.length() == 0) {
    error = cronFieldError(field, "field cannot be empty");
    return false;
  }
  if (text.length() > 12) {
    error = cronFieldError(field, "expression is too long");
    return false;
  }

  int16_t itemStart = 0;
  while (itemStart < static_cast<int16_t>(text.length())) {
    int16_t comma = text.indexOf(',', itemStart);
    if (comma < 0) {
      comma = static_cast<int16_t>(text.length());
    }
    if (comma == itemStart) {
      error = cronFieldError(field, "empty list item");
      return false;
    }

    const String item = text.substring(itemStart, comma);
    int16_t slash = item.indexOf('/');
    if (slash >= 0 && item.indexOf('/', slash + 1) >= 0) {
      error = cronFieldError(field, "only one step is allowed");
      return false;
    }

    String base = item;
    uint16_t step = 1;
    const bool hasStep = slash >= 0;
    if (hasStep) {
      base = item.substring(0, slash);
      const String stepText = item.substring(slash + 1);
      if (!parseCronUnsigned(stepText, step) || step == 0 || step > maximumStep) {
        char detail[34];
        std::snprintf(detail, sizeof(detail), "step must be 1-%u", static_cast<unsigned>(maximumStep));
        error = cronFieldError(field, detail);
        return false;
      }
    }

    uint16_t rangeStart = 0;
    uint16_t rangeEnd = 0;
    if (base == "*") {
      rangeStart = minimum;
      rangeEnd = maximum;
    } else {
      const int16_t dash = base.indexOf('-');
      if (dash >= 0 && base.indexOf('-', dash + 1) >= 0) {
        error = cronFieldError(field, "only one range is allowed");
        return false;
      }
      if (dash >= 0) {
        const String startText = base.substring(0, dash);
        const String endText = base.substring(dash + 1);
        if (!parseCronUnsigned(startText, rangeStart) || !parseCronUnsigned(endText, rangeEnd)) {
          error = cronFieldError(field, "invalid range syntax");
          return false;
        }
        if (rangeStart > rangeEnd) {
          error = cronFieldError(field, "range start must be <= end");
          return false;
        }
      } else {
        if (!parseCronUnsigned(base, rangeStart)) {
          error = cronFieldError(field, "use numbers, *, /, - or ,");
          return false;
        }
        rangeEnd = hasStep ? maximum : rangeStart;
      }
    }

    if (rangeStart < minimum || rangeStart > maximum || rangeEnd < minimum || rangeEnd > maximum) {
      char detail[34];
      std::snprintf(detail, sizeof(detail), "value must be %u-%u", static_cast<unsigned>(minimum),
                    static_cast<unsigned>(maximum));
      error = cronFieldError(field, detail);
      return false;
    }

    for (uint16_t value = rangeStart; value <= rangeEnd; value = static_cast<uint16_t>(value + step)) {
      const uint8_t normalized = field == 4 && value == 7 ? 0 : static_cast<uint8_t>(value);
      mask |= (static_cast<uint64_t>(1) << normalized);
      if (rangeEnd - value < step) {
        break;
      }
    }

    itemStart = comma + 1;
    if (itemStart == static_cast<int16_t>(text.length())) {
      error = cronFieldError(field, "list cannot end with comma");
      return false;
    }
  }

  if (mask == 0) {
    error = cronFieldError(field, "does not select any value");
    return false;
  }
  return true;
}

bool validateCronFields(const String fields[5], CronSchedule& schedule, uint8_t& invalidField, String& error) {
  schedule.domWildcard = false;
  schedule.dowWildcard = false;
  for (uint8_t field = 0; field < 5; ++field) {
    bool wildcard = false;
    if (!parseCronField(fields[field], field, schedule.masks[field], wildcard, error)) {
      invalidField = field;
      return false;
    }
    if (field == 2) {
      schedule.domWildcard = wildcard;
    } else if (field == 4) {
      schedule.dowWildcard = wildcard;
    }
  }
  invalidField = 0;
  error = "";
  return true;
}

bool cronMaskContains(uint64_t mask, uint8_t value) {
  return (mask & (static_cast<uint64_t>(1) << value)) != 0;
}

uint8_t cronMaskCount(uint64_t mask, uint8_t minimum, uint8_t maximum) {
  uint8_t count = 0;
  for (uint16_t value = minimum; value <= maximum; ++value) {
    if (cronMaskContains(mask, static_cast<uint8_t>(value))) {
      ++count;
    }
  }
  return count;
}

bool cronMaskSelectsAll(uint64_t mask, uint8_t minimum, uint8_t maximum) {
  return cronMaskCount(mask, minimum, maximum) == static_cast<uint8_t>(maximum - minimum + 1U);
}

int16_t cronSingleValue(uint64_t mask, uint8_t minimum, uint8_t maximum) {
  int16_t selected = -1;
  for (uint16_t value = minimum; value <= maximum; ++value) {
    if (!cronMaskContains(mask, static_cast<uint8_t>(value))) {
      continue;
    }
    if (selected >= 0) {
      return -1;
    }
    selected = static_cast<int16_t>(value);
  }
  return selected;
}

bool cronRegularSequence(uint64_t mask, uint8_t minimum, uint8_t maximum, uint8_t& first,
                         uint8_t& last, uint8_t& step, uint8_t& count) {
  count = 0;
  step = 0;
  uint8_t previous = 0;
  for (uint16_t value = minimum; value <= maximum; ++value) {
    if (!cronMaskContains(mask, static_cast<uint8_t>(value))) {
      continue;
    }
    const uint8_t current = static_cast<uint8_t>(value);
    if (count == 0) {
      first = current;
    } else if (count == 1) {
      step = static_cast<uint8_t>(current - previous);
    } else if (static_cast<uint8_t>(current - previous) != step) {
      return false;
    }
    previous = current;
    last = current;
    ++count;
  }
  return count >= 2 && step > 0;
}

String twoDigitCronValue(uint8_t value) {
  char buffer[4];
  std::snprintf(buffer, sizeof(buffer), "%02u", static_cast<unsigned>(value));
  return String(buffer);
}

void appendNaturalListItem(String& result, const String& item, uint8_t itemIndex, uint8_t itemCount) {
  if (itemIndex > 0) {
    result += itemIndex + 1U == itemCount ? " and " : ", ";
  }
  result += item;
}

String cronNumericSelection(uint64_t mask, uint8_t minimum, uint8_t maximum, uint8_t maxItems,
                            bool zeroPad = false) {
  const uint8_t total = cronMaskCount(mask, minimum, maximum);
  if (total == 0 || total > maxItems) {
    return "selected values";
  }

  String result;
  uint8_t index = 0;
  for (uint16_t value = minimum; value <= maximum; ++value) {
    if (!cronMaskContains(mask, static_cast<uint8_t>(value))) {
      continue;
    }
    const String item = zeroPad ? twoDigitCronValue(static_cast<uint8_t>(value))
                                : String(static_cast<unsigned>(value));
    appendNaturalListItem(result, item, index, total);
    ++index;
  }
  return result;
}

String cronCompactRanges(uint64_t mask, uint8_t minimum, uint8_t maximum, bool zeroPad = false) {
  String ranges[8];
  uint8_t rangeCount = 0;
  uint16_t value = minimum;
  while (value <= maximum) {
    if (!cronMaskContains(mask, static_cast<uint8_t>(value))) {
      ++value;
      continue;
    }

    const uint8_t start = static_cast<uint8_t>(value);
    uint8_t end = start;
    while (value + 1U <= maximum &&
           cronMaskContains(mask, static_cast<uint8_t>(value + 1U))) {
      ++value;
      end = static_cast<uint8_t>(value);
    }

    if (rangeCount >= 8) {
      return "selected values";
    }
    const String startText = zeroPad ? twoDigitCronValue(start) : String(static_cast<unsigned>(start));
    if (start == end) {
      ranges[rangeCount] = startText;
    } else {
      const String endText = zeroPad ? twoDigitCronValue(end) : String(static_cast<unsigned>(end));
      ranges[rangeCount] = startText + "-" + endText;
    }
    ++rangeCount;
    ++value;
  }

  String result;
  for (uint8_t index = 0; index < rangeCount; ++index) {
    appendNaturalListItem(result, ranges[index], index, rangeCount);
  }
  return result;
}

String cronMinuteSelection(uint64_t mask) {
  if (cronMaskSelectsAll(mask, 0, 59)) {
    return "every minute";
  }

  const int16_t single = cronSingleValue(mask, 0, 59);
  if (single >= 0) {
    return "minute " + String(static_cast<int>(single));
  }

  uint8_t first = 0;
  uint8_t last = 0;
  uint8_t step = 0;
  uint8_t count = 0;
  if (cronRegularSequence(mask, 0, 59, first, last, step, count)) {
    if (step == 1) {
      return "minutes " + String(static_cast<unsigned>(first)) + "-" +
             String(static_cast<unsigned>(last));
    }
    if (static_cast<uint16_t>(last) + step > 59U) {
      if (first == 0) {
        return "every " + String(static_cast<unsigned>(step)) + " minutes";
      }
      return "every " + String(static_cast<unsigned>(step)) + " minutes from minute " +
             String(static_cast<unsigned>(first));
    }
    return "every " + String(static_cast<unsigned>(step)) + " minutes from " +
           String(static_cast<unsigned>(first)) + " to " + String(static_cast<unsigned>(last));
  }

  const uint8_t total = cronMaskCount(mask, 0, 59);
  if (total <= 6) {
    return "minutes " + cronNumericSelection(mask, 0, 59, 6);
  }
  return "minutes " + cronCompactRanges(mask, 0, 59);
}

String cronHourSelection(uint64_t mask) {
  if (cronMaskSelectsAll(mask, 0, 23)) {
    return "all hours";
  }

  const int16_t single = cronSingleValue(mask, 0, 23);
  if (single >= 0) {
    return "hour " + twoDigitCronValue(static_cast<uint8_t>(single));
  }

  uint8_t first = 0;
  uint8_t last = 0;
  uint8_t step = 0;
  uint8_t count = 0;
  if (cronRegularSequence(mask, 0, 23, first, last, step, count)) {
    if (step == 1) {
      return "hours " + twoDigitCronValue(first) + "-" + twoDigitCronValue(last);
    }
    return "hours " + twoDigitCronValue(first) + "-" + twoDigitCronValue(last) + " every " +
           String(static_cast<unsigned>(step)) + " hours";
  }

  const uint8_t total = cronMaskCount(mask, 0, 23);
  if (total <= 6) {
    return "hours " + cronNumericSelection(mask, 0, 23, 6, true);
  }
  return "hours " + cronCompactRanges(mask, 0, 23, true);
}

String describeCronTime(const CronSchedule& schedule) {
  const uint64_t minuteMask = schedule.masks[0];
  const uint64_t hourMask = schedule.masks[1];
  const bool everyMinute = cronMaskSelectsAll(minuteMask, 0, 59);
  const bool everyHour = cronMaskSelectsAll(hourMask, 0, 23);
  const int16_t singleMinute = cronSingleValue(minuteMask, 0, 59);
  const int16_t singleHour = cronSingleValue(hourMask, 0, 23);
  const uint8_t minuteCount = cronMaskCount(minuteMask, 0, 59);
  const uint8_t hourCount = cronMaskCount(hourMask, 0, 23);

  if (everyMinute && everyHour) {
    return "Run every minute";
  }

  if (everyHour) {
    if (singleMinute >= 0) {
      return "Run every hour at minute " + String(static_cast<int>(singleMinute));
    }
    const String minutes = cronMinuteSelection(minuteMask);
    if (minutes.substring(0, 5) == "every") {
      return "Run " + minutes;
    }
    return "Run every hour at " + minutes;
  }

  if (everyMinute) {
    return "Run every minute during " + cronHourSelection(hourMask);
  }

  if (singleMinute >= 0 && singleHour >= 0) {
    return "Run at " + formatHhmm(static_cast<uint8_t>(singleHour),
                                   static_cast<uint8_t>(singleMinute));
  }

  if (singleMinute >= 0) {
    uint8_t first = 0;
    uint8_t last = 0;
    uint8_t step = 0;
    uint8_t count = 0;
    if (cronRegularSequence(hourMask, 0, 23, first, last, step, count)) {
      if (step == 1) {
        return "Run every hour at minute " + String(static_cast<int>(singleMinute)) +
               " from " + twoDigitCronValue(first) + " to " + twoDigitCronValue(last);
      }
      if (first == 0 && static_cast<uint16_t>(last) + step > 23U) {
        return "Run every " + String(static_cast<unsigned>(step)) + " hours at minute " +
               String(static_cast<int>(singleMinute));
      }
      return "Run every " + String(static_cast<unsigned>(step)) + " hours at minute " +
             String(static_cast<int>(singleMinute)) + " from " + twoDigitCronValue(first) +
             " to " + twoDigitCronValue(last);
    }

    if (hourCount <= 6) {
      String result = "Run at ";
      uint8_t index = 0;
      for (uint8_t hour = 0; hour < 24; ++hour) {
        if (!cronMaskContains(hourMask, hour)) {
          continue;
        }
        appendNaturalListItem(result,
                              formatHhmm(hour, static_cast<uint8_t>(singleMinute)), index,
                              hourCount);
        ++index;
      }
      return result;
    }
    return "Run at minute " + String(static_cast<int>(singleMinute)) + " in selected hours";
  }

  if (singleHour >= 0) {
    const String minutes = cronMinuteSelection(minuteMask);
    if (minutes.substring(0, 5) == "every") {
      return "Run " + minutes + " during hour " +
             twoDigitCronValue(static_cast<uint8_t>(singleHour));
    }
    if (minuteCount <= 6) {
      String result = "Run at ";
      uint8_t index = 0;
      for (uint8_t minute = 0; minute < 60; ++minute) {
        if (!cronMaskContains(minuteMask, minute)) {
          continue;
        }
        appendNaturalListItem(result,
                              formatHhmm(static_cast<uint8_t>(singleHour), minute), index,
                              minuteCount);
        ++index;
      }
      return result;
    }
    return "Run at " + minutes + " during hour " +
           twoDigitCronValue(static_cast<uint8_t>(singleHour));
  }

  if (static_cast<uint16_t>(minuteCount) * hourCount <= 6U) {
    const uint8_t total = static_cast<uint8_t>(minuteCount * hourCount);
    String result = "Run at ";
    uint8_t index = 0;
    for (uint8_t hour = 0; hour < 24; ++hour) {
      if (!cronMaskContains(hourMask, hour)) {
        continue;
      }
      for (uint8_t minute = 0; minute < 60; ++minute) {
        if (!cronMaskContains(minuteMask, minute)) {
          continue;
        }
        appendNaturalListItem(result, formatHhmm(hour, minute), index, total);
        ++index;
      }
    }
    return result;
  }

  const String minutes = cronMinuteSelection(minuteMask);
  if (minutes.substring(0, 5) == "every") {
    return "Run " + minutes + " in " + cronHourSelection(hourMask);
  }
  return "Run at " + minutes + " in " + cronHourSelection(hourMask);
}

String cronWeekdaySelection(uint64_t mask) {
  static const char* FULL_NAMES[7] = {
      "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
  };
  static const char* SHORT_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

  const uint64_t weekdayMask = (static_cast<uint64_t>(1) << 1) | (static_cast<uint64_t>(1) << 2) |
                               (static_cast<uint64_t>(1) << 3) | (static_cast<uint64_t>(1) << 4) |
                               (static_cast<uint64_t>(1) << 5);
  const uint64_t weekendMask = (static_cast<uint64_t>(1) << 0) | (static_cast<uint64_t>(1) << 6);
  if (mask == weekdayMask) {
    return "Monday-Friday";
  }
  if (mask == weekendMask) {
    return "Saturday and Sunday";
  }
  if (cronMaskSelectsAll(mask, 0, 6)) {
    return "every day";
  }

  uint8_t first = 0;
  uint8_t last = 0;
  uint8_t step = 0;
  uint8_t count = 0;
  if (cronRegularSequence(mask, 0, 6, first, last, step, count) && step == 1 && count >= 3) {
    return String(FULL_NAMES[first]) + "-" + FULL_NAMES[last];
  }

  const uint8_t total = cronMaskCount(mask, 0, 6);
  String result;
  uint8_t index = 0;
  for (uint8_t day = 0; day < 7; ++day) {
    if (!cronMaskContains(mask, day)) {
      continue;
    }
    const char* name = total <= 3 ? FULL_NAMES[day] : SHORT_NAMES[day];
    appendNaturalListItem(result, String(name), index, total);
    ++index;
  }
  return result;
}

String cronMonthSelection(uint64_t mask) {
  static const char* FULL_NAMES[12] = {
      "January", "February", "March",     "April",   "May",      "June",
      "July",    "August",   "September", "October", "November", "December",
  };
  static const char* SHORT_NAMES[12] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
  };

  if (cronMaskSelectsAll(mask, 1, 12)) {
    return "every month";
  }

  const int16_t single = cronSingleValue(mask, 1, 12);
  if (single >= 1) {
    return FULL_NAMES[single - 1];
  }

  uint8_t first = 0;
  uint8_t last = 0;
  uint8_t step = 0;
  uint8_t count = 0;
  if (cronRegularSequence(mask, 1, 12, first, last, step, count) && step == 1) {
    return String(FULL_NAMES[first - 1]) + "-" + FULL_NAMES[last - 1];
  }

  const uint8_t total = cronMaskCount(mask, 1, 12);
  if (total > 6) {
    return "selected months";
  }
  String result;
  uint8_t index = 0;
  for (uint8_t month = 1; month <= 12; ++month) {
    if (!cronMaskContains(mask, month)) {
      continue;
    }
    const char* name = total <= 2 ? FULL_NAMES[month - 1] : SHORT_NAMES[month - 1];
    appendNaturalListItem(result, String(name), index, total);
    ++index;
  }
  return result;
}

String cronDayOfMonthSelection(uint64_t mask) {
  const int16_t single = cronSingleValue(mask, 1, 31);
  if (single >= 1) {
    return "day " + String(static_cast<int>(single));
  }

  const uint8_t total = cronMaskCount(mask, 1, 31);
  if (total == 2) {
    return "days " + cronNumericSelection(mask, 1, 31, 2);
  }

  uint8_t first = 0;
  uint8_t last = 0;
  uint8_t step = 0;
  uint8_t count = 0;
  if (cronRegularSequence(mask, 1, 31, first, last, step, count)) {
    if (step == 1) {
      return "days " + String(static_cast<unsigned>(first)) + "-" +
             String(static_cast<unsigned>(last));
    }
    String result = "every " + String(static_cast<unsigned>(step)) + " days from day " +
                    String(static_cast<unsigned>(first));
    if (static_cast<uint16_t>(last) + step <= 31U) {
      result += " to " + String(static_cast<unsigned>(last));
    }
    return result;
  }

  if (total <= 6) {
    return "days " + cronNumericSelection(mask, 1, 31, 6);
  }
  return "days " + cronCompactRanges(mask, 1, 31);
}

String describeCronDate(const CronSchedule& schedule) {
  const bool everyMonth = cronMaskSelectsAll(schedule.masks[3], 1, 12);
  const bool domMaskFull = cronMaskSelectsAll(schedule.masks[2], 1, 31);
  const bool dowMaskFull = cronMaskSelectsAll(schedule.masks[4], 0, 6);

  bool everyDay = false;
  if (schedule.domWildcard) {
    everyDay = schedule.dowWildcard || dowMaskFull;
  } else if (schedule.dowWildcard) {
    everyDay = domMaskFull;
  } else {
    // Traditional five-field Cron treats restricted DOM and DOW as OR.
    everyDay = domMaskFull || dowMaskFull;
  }

  String result;
  if (everyDay) {
    result = "every day";
  } else if (schedule.domWildcard) {
    result = "on " + cronWeekdaySelection(schedule.masks[4]);
  } else if (schedule.dowWildcard) {
    result = "on " + cronDayOfMonthSelection(schedule.masks[2]);
    if (everyMonth) {
      result += " of every month";
    }
  } else {
    result = "on " + cronDayOfMonthSelection(schedule.masks[2]) + " or on " +
             cronWeekdaySelection(schedule.masks[4]);
  }

  if (!everyMonth) {
    result += " in " + cronMonthSelection(schedule.masks[3]);
  }
  return result;
}

String formatCronDescription(const CronSchedule& schedule) {
  const String time = describeCronTime(schedule);
  const String date = describeCronDate(schedule);
  if (date == "every day") {
    return time + ", every day.";
  }
  return time + " " + date + ".";
}

String capitalizeCronClause(String text) {
  if (text.length() > 0 && text[0] >= 'a' && text[0] <= 'z') {
    text[0] = static_cast<char>(text[0] - ('a' - 'A'));
  }
  return text;
}

void splitCronTextAtWord(const String& text, String& firstLine, String& secondLine) {
  constexpr uint8_t MAX_LINE = 48;
  if (text.length() <= MAX_LINE) {
    firstLine = text;
    secondLine = "";
    return;
  }

  int16_t splitAt = -1;
  const char* preferred[] = {" in ", " during ", " on ", ", every ", " from "};
  for (uint8_t markerIndex = 0; markerIndex < sizeof(preferred) / sizeof(preferred[0]); ++markerIndex) {
    int16_t position = text.indexOf(preferred[markerIndex]);
    while (position >= 0) {
      const uint16_t firstLength = static_cast<uint16_t>(position);
      const uint16_t secondLength = static_cast<uint16_t>(text.length() - position - 1U);
      if (firstLength >= 18U && firstLength <= MAX_LINE && secondLength <= MAX_LINE) {
        splitAt = position;
      }
      position = text.indexOf(preferred[markerIndex], static_cast<unsigned>(position + 1));
    }
    if (splitAt >= 0) {
      break;
    }
  }

  if (splitAt < 0) {
    const uint16_t searchLimit = text.length() < MAX_LINE ? text.length() : MAX_LINE;
    for (uint16_t i = 0; i < searchLimit; ++i) {
      if (text[i] == ' ') {
        splitAt = static_cast<int16_t>(i);
      }
    }
  }
  if (splitAt < 1) {
    splitAt = MAX_LINE;
  }

  firstLine = text.substring(0, static_cast<unsigned>(splitAt));
  uint16_t secondStart = static_cast<uint16_t>(splitAt);
  while (secondStart < text.length() && text[secondStart] == ' ') {
    ++secondStart;
  }
  secondLine = trimForDisplay(text.substring(secondStart), MAX_LINE);
}

bool cronStatusLines(const String fields[5], String& line1, String& line2, String& line3,
                     uint8_t& invalidField) {
  CronSchedule schedule{};
  String error;
  if (!validateCronFields(fields, schedule, invalidField, error)) {
    line1 = "Fix expression";
    line2 = error;
    line3 = "";
    return false;
  }

  const String time = describeCronTime(schedule);
  const String date = describeCronDate(schedule);
  const String full = formatCronDescription(schedule);
  if (full.length() <= 48) {
    line1 = full;
    line2 = "";
    line3 = "";
    return true;
  }

  if (time.length() <= 48 && date.length() + 1U <= 48) {
    line1 = time;
    line2 = capitalizeCronClause(date) + ".";
    line3 = "";
    return true;
  }

  if (time.length() > 48) {
    splitCronTextAtWord(time, line1, line2);
    line3 = trimForDisplay(capitalizeCronClause(date) + ".", 48);
    return true;
  }

  line1 = time;
  const String dateSentence = capitalizeCronClause(date) + ".";
  splitCronTextAtWord(dateSentence, line2, line3);
  return true;
}

Rect cronFieldRect(uint8_t field, int16_t y = 42) {
  return {static_cast<int16_t>(12 + field * 61), y, 52, 31};
}

const char* cronFieldLabel(uint8_t field) {
  const char* labels[5] = {"MIN", "HOUR", "DOM", "MON", "DOW"};
  return labels[field];
}

void drawCronFieldBoxes(const String fields[5], int16_t y, int8_t selectedField = -1,
                        int8_t invalidField = -1) {
  for (uint8_t i = 0; i < 5; ++i) {
    const Rect box = cronFieldRect(i, y);
    const bool selected = selectedField == static_cast<int8_t>(i);
    const bool invalid = invalidField == static_cast<int8_t>(i);
    const uint16_t bg = selected ? COLOR_ACCENT_DARK : COLOR_PANEL;
    const uint16_t border = invalid ? COLOR_DANGER : (selected ? COLOR_ACCENT : COLOR_BORDER);
    drawRoundedPanel(box, bg, border, 5);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOR_TEXT, bg);
    tft.setTextFont(fields[i].length() <= 4 ? 2 : 1);
    tft.drawString(trimForDisplay(fields[i], 8), box.x + box.w / 2, box.y + box.h / 2 + 1);

    tft.setTextFont(1);
    tft.setTextColor(invalid ? COLOR_DANGER : (selected ? COLOR_ACCENT : COLOR_MUTED), COLOR_BG);
    tft.drawString(cronFieldLabel(i), box.x + box.w / 2, box.y + box.h + 8);
  }
}

Rect cronMinusRect(uint8_t row) {
  return {12, static_cast<int16_t>(158 + row * 33), 32, 28};
}

Rect cronCenterRect(uint8_t row) {
  return {50, static_cast<int16_t>(158 + row * 33), 220, 28};
}

Rect cronPlusRect(uint8_t row) {
  return {276, static_cast<int16_t>(158 + row * 33), 32, 28};
}

void drawCronParamRow(uint8_t row, const char* label, const String& value, bool active) {
  drawButton(cronMinusRect(row), active ? "-" : "", active ? COLOR_RAISED : COLOR_PANEL,
             active ? COLOR_TEXT : COLOR_MUTED, 2);
  Rect center = cronCenterRect(row);
  drawRoundedPanel(center, active ? COLOR_PANEL : COLOR_BG, active ? COLOR_BORDER : COLOR_BG, 6);
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_MUTED, active ? COLOR_PANEL : COLOR_BG);
  tft.drawString(label, center.x + 8, center.y + 14);
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(value.length() > 18 ? 1 : 2);
  tft.setTextColor(active ? COLOR_TEXT : COLOR_MUTED, active ? COLOR_PANEL : COLOR_BG);
  tft.drawString(value, center.x + center.w - 8, center.y + 14);
  drawButton(cronPlusRect(row), active ? "+" : "", active ? COLOR_RAISED : COLOR_PANEL,
             active ? COLOR_TEXT : COLOR_MUTED, 2);
}

void drawCron() {
  tft.fillScreen(COLOR_BG);
  drawHeader("CRON GENERATOR");

  String fields[5];
  currentCronFields(fields);
  drawCronFieldBoxes(fields, 42);

  String cronLine1;
  String cronLine2;
  String cronLine3;
  uint8_t invalidField = 0;
  const bool cronValid = cronStatusLines(fields, cronLine1, cronLine2, cronLine3, invalidField);
  Rect meaningPanel{12, 86, 296, 33};
  const uint16_t meaningBackground = cronValid ? COLOR_PANEL_2 : COLOR_DANGER;
  drawRoundedPanel(meaningPanel, meaningBackground);
  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_TEXT, meaningBackground);
  if (cronLine2.length() == 0) {
    tft.drawString(trimForDisplay(cronLine1, 48), SCREEN_W / 2, 103);
  } else if (cronLine3.length() == 0) {
    tft.drawString(trimForDisplay(cronLine1, 48), SCREEN_W / 2, 97);
    tft.drawString(trimForDisplay(cronLine2, 48), SCREEN_W / 2, 110);
  } else {
    tft.drawString(trimForDisplay(cronLine1, 48), SCREEN_W / 2, 92);
    tft.drawString(trimForDisplay(cronLine2, 48), SCREEN_W / 2, 103);
    tft.drawString(trimForDisplay(cronLine3, 48), SCREEN_W / 2, 114);
  }

  drawRoundedPanel(CRON_PRESET_BUTTON, COLOR_PANEL, COLOR_ACCENT, 6);
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.drawString("PRESET", 20, 137);
  tft.setTextFont(2);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawString(cronPresetName(), 299, 137);

  if (cronCustom) {
    drawCronParamRow(0, "Manual mode", "tap a field above", false);
    drawCronParamRow(1, "Symbols", "*  /  -  ,", false);
  } else if (cronPreset == 0) {
    drawCronParamRow(0, "Interval", String(cronIntervalMinutes) + " min", true);
    drawCronParamRow(1, "Edit expression", "tap fields above", false);
  } else if (cronPreset == 1) {
    drawCronParamRow(0, "Minute", String(cronMinute), true);
    drawCronParamRow(1, "Hour", "every hour", false);
  } else if (cronPreset == 6) {
    drawCronParamRow(0, "Fixed", "0 */6 * * *", false);
    drawCronParamRow(1, "Edit expression", "tap fields above", false);
  } else {
    drawCronParamRow(0, "Hour", String(cronHour), true);
    drawCronParamRow(1, "Minute", String(cronMinute), true);
  }
}

Rect cronEditKeyRect(uint8_t index) {
  const uint8_t col = index % 5;
  const uint8_t row = index / 5;
  return {static_cast<int16_t>(12 + col * 61), static_cast<int16_t>(94 + row * 34), 55, 29};
}

Rect cronEditClearRect() {
  return {12, 199, 92, 34};
}

Rect cronEditCancelRect() {
  return {114, 199, 92, 34};
}

Rect cronEditApplyRect() {
  return {216, 199, 92, 34};
}

void startCronEditor(uint8_t selectedField) {
  String fields[5];
  currentCronFields(fields);
  for (uint8_t i = 0; i < 5; ++i) {
    cronEditFields[i] = fields[i];
  }
  cronEditSelectedField = selectedField < 5 ? selectedField : 0;
  cronEditInvalidField = -1;
  cronEditError = "";
  currentScreen = Screen::CronEdit;
}

void drawCronEdit() {
  tft.fillScreen(COLOR_BG);
  drawHeader("EDIT CRON FIELDS", true, "CRON");
  drawCronFieldBoxes(cronEditFields, 42, cronEditSelectedField, cronEditInvalidField);

  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(cronEditError.length() > 0 ? COLOR_DANGER : COLOR_MUTED, COLOR_BG);
  String helper = cronEditError;
  if (helper.length() == 0) {
    static const uint8_t minimums[5] = {0, 0, 1, 1, 0};
    static const uint8_t maximums[5] = {59, 23, 31, 12, 7};
    char hint[52];
    if (cronEditSelectedField == 4) {
      std::snprintf(hint, sizeof(hint), "DOW 0-7 (0/7=Sun) | use * / - ,");
    } else {
      std::snprintf(hint, sizeof(hint), "%s %u-%u | use * / - ,", cronFieldLabel(cronEditSelectedField),
                    static_cast<unsigned>(minimums[cronEditSelectedField]),
                    static_cast<unsigned>(maximums[cronEditSelectedField]));
    }
    helper = hint;
  }
  tft.drawString(trimForDisplay(helper, 49), SCREEN_W / 2, 89);

  const char* keys[15] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "*", "/", "-", ",", "DEL"};
  for (uint8_t i = 0; i < 15; ++i) {
    drawButton(cronEditKeyRect(i), keys[i], COLOR_RAISED, COLOR_TEXT, i == 14 ? 1 : 2);
  }

  drawButton(cronEditClearRect(), "CLEAR", COLOR_PANEL, COLOR_MUTED, 1);
  drawButton(cronEditCancelRect(), "CANCEL", COLOR_DANGER, COLOR_TEXT, 1);
  drawButton(cronEditApplyRect(), "APPLY", cronEditError.length() > 0 ? COLOR_WARNING : COLOR_SUCCESS,
             COLOR_TEXT, 1);
}

Rect capacityMinusRect(uint8_t row) {
  return {12, static_cast<int16_t>(68 + row * 25), 26, 23};
}

Rect capacityCenterRect(uint8_t row) {
  return {43, static_cast<int16_t>(68 + row * 25), 234, 23};
}

Rect capacityPlusRect(uint8_t row) {
  return {282, static_cast<int16_t>(68 + row * 25), 26, 23};
}

Rect capacityChoiceRect(uint8_t row) {
  return {12, static_cast<int16_t>(68 + row * 25), 265, 23};
}

void drawCapacityRow(uint8_t row, const char* label, const String& value) {
  drawButton(capacityMinusRect(row), "-", COLOR_RAISED, COLOR_TEXT, 2);
  Rect center = capacityCenterRect(row);
  drawRoundedPanel(center, COLOR_PANEL, COLOR_BORDER, 5);
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.drawString(label, center.x + 7, center.y + 12);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawString(value, center.x + center.w - 7, center.y + 12);
  drawButton(capacityPlusRect(row), "+", COLOR_RAISED, COLOR_TEXT, 2);
}

void drawCapacityChoiceRow(uint8_t row, const char* label, const String& value) {
  const Rect center = capacityChoiceRect(row);
  drawRoundedPanel(center, COLOR_PANEL, COLOR_ACCENT, 5);
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.drawString(label, center.x + 7, center.y + 12);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawString(value, center.x + center.w - 7, center.y + 12);
  drawButton(capacityPlusRect(row), ">", COLOR_ACCENT_DARK, COLOR_TEXT, 2, COLOR_ACCENT);
}

BackupScheme activeBackupScheme() {
  if (backupSchemeIndex == BACKUP_SCHEME_COUNT - 1) {
    return {"CUSTOM", backupCustomDaily, backupCustomWeekly, backupCustomMonthly};
  }
  return BACKUP_SCHEMES[backupSchemeIndex];
}

String backupSchemeDisplay() {
  if (backupSchemeIndex != BACKUP_SCHEME_COUNT - 1) {
    return String(BACKUP_SCHEMES[backupSchemeIndex].label);
  }
  char label[28];
  std::snprintf(label, sizeof(label), "CUSTOM %uD/%uW/%uM", static_cast<unsigned>(backupCustomDaily),
                static_cast<unsigned>(backupCustomWeekly), static_cast<unsigned>(backupCustomMonthly));
  return String(label);
}

void drawCapacity() {
  tft.fillScreen(COLOR_BG);
  drawHeader("CAPACITY PLANNER");

  drawButton(STORAGE_TAB_STORAGE, "STORAGE", storageTab == StorageTab::Storage ? COLOR_ACCENT_DARK : COLOR_RAISED,
             COLOR_TEXT, 2, storageTab == StorageTab::Storage ? COLOR_ACCENT : COLOR_BORDER);
  drawButton(STORAGE_TAB_BACKUP, "BACKUP", storageTab == StorageTab::Backup ? COLOR_ACCENT_DARK : COLOR_RAISED,
             COLOR_TEXT, 2, storageTab == StorageTab::Backup ? COLOR_ACCENT : COLOR_BORDER);

  if (storageTab == StorageTab::Storage) {
    drawCapacityRow(0, "Daily data", String(storageDailyGb) + " GB/day");
    drawCapacityRow(1, "Retention", String(storageRetentionDays) + " days");
    drawCapacityRow(2, "Compression", String(storageCompressionPct) + "% of source");
    drawCapacityRow(3, "Copies", String(storageCopies));
    drawCapacityRow(4, "Free space", String(storageSparePct) + "% spare");

    const double raw = static_cast<double>(storageDailyGb) * storageRetentionDays *
                       (static_cast<double>(storageCompressionPct) / 100.0) * storageCopies;
    const double total = raw * (1.0 + static_cast<double>(storageSparePct) / 100.0);
    Rect result{12, 197, 296, 37};
    drawRoundedPanel(result, COLOR_PANEL);
    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_SUCCESS, COLOR_PANEL);
    tft.drawString("Required", 20, 209);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(formatSizeGb(total), 300, 209);
    tft.setTextFont(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft.drawString("Raw before spare", 20, 225);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(formatSizeGb(raw), 300, 225);
  } else {
    const BackupScheme scheme = activeBackupScheme();
    drawCapacityRow(0, "Full backup", String(backupFullGb) + " GB");
    drawCapacityRow(1, "Incremental", String(backupIncrementalGb) + " GB");
    drawCapacityChoiceRow(2, "Frequency", String(BACKUP_FREQS[backupFrequencyIndex]) + " / day");
    drawCapacityChoiceRow(3, "Scheme", backupSchemeDisplay());
    drawCapacityRow(4, "Copies", String(backupCopies));

    const uint16_t incPoints = static_cast<uint16_t>(scheme.daily) * BACKUP_FREQS[backupFrequencyIndex];
    const uint16_t fullPoints = static_cast<uint16_t>(scheme.weekly) + scheme.monthly + 1;
    const double total = (static_cast<double>(fullPoints) * backupFullGb +
                          static_cast<double>(incPoints) * backupIncrementalGb) * backupCopies;
    Rect result{12, 197, 296, 37};
    drawRoundedPanel(result, COLOR_PANEL);
    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_SUCCESS, COLOR_PANEL);
    tft.drawString("Required", 20, 209);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(formatSizeGb(total), 300, 209);
    char points[48];
    std::snprintf(points, sizeof(points), "Full points %u  Incrementals %u", static_cast<unsigned>(fullPoints),
                  static_cast<unsigned>(incPoints));
    tft.setTextFont(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
    tft.drawString(points, 20, 225);
  }
}

Rect retentionMinusRect(uint8_t row) {
  return {12, static_cast<int16_t>(70 + row * 43), 38, 34};
}

Rect retentionCenterRect(uint8_t row) {
  return {58, static_cast<int16_t>(70 + row * 43), 204, 34};
}

Rect retentionPlusRect(uint8_t row) {
  return {270, static_cast<int16_t>(70 + row * 43), 38, 34};
}

void drawRetentionRow(uint8_t row, const char* label, uint8_t value) {
  drawButton(retentionMinusRect(row), "-", COLOR_RAISED, COLOR_TEXT, 2);
  const Rect center = retentionCenterRect(row);
  drawRoundedPanel(center, COLOR_PANEL, COLOR_BORDER, 6);
  tft.setTextFont(1);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.drawString(label, center.x + 9, center.y + 17);
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawString(String(value), center.x + center.w - 9, center.y + 17);
  drawButton(retentionPlusRect(row), "+", COLOR_RAISED, COLOR_TEXT, 2);
}

void drawBackupRetention() {
  tft.fillScreen(COLOR_BG);
  drawHeader("CUSTOM RETENTION", true, "BACK");

  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("Tap a value to type exact restore points", SCREEN_W / 2, 52);

  drawRetentionRow(0, "Daily restore points", backupCustomDaily);
  drawRetentionRow(1, "Weekly restore points", backupCustomWeekly);
  drawRetentionRow(2, "Monthly restore points", backupCustomMonthly);

  drawButton(RETENTION_APPLY, "APPLY TO BACKUP", COLOR_SUCCESS, COLOR_TEXT, 1);
}

void drawChmod() {
  tft.fillScreen(COLOR_BG);
  drawHeader("CHMOD CALCULATOR");

  Rect resultPanel{12, 41, 296, 38};
  drawRoundedPanel(resultPanel, COLOR_PANEL);
  char numeric[16];
  std::snprintf(numeric, sizeof(numeric), "chmod %u%u%u", chmodValueForColumn(0),
                chmodValueForColumn(1), chmodValueForColumn(2));
  tft.setTextColor(COLOR_SUCCESS, COLOR_PANEL);
  tft.setTextFont(4);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(numeric, 21, 60);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.setTextFont(1);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("tap to type", 300, 51);
  tft.setTextFont(2);
  tft.drawString(chmodSymbolic(), 300, 67);

  const char* columnLabels[] = {"OWNER", "GROUP", "OTHER"};
  for (uint8_t col = 0; col < 3; ++col) {
    tft.setTextColor(COLOR_MUTED, COLOR_BG);
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(columnLabels[col], 61 + col * 100, 88);
  }

  const char* rowLabels[] = {"R", "W", "X"};
  for (uint8_t row = 0; row < 3; ++row) {
    for (uint8_t col = 0; col < 3; ++col) {
      Rect cell{static_cast<int16_t>(12 + col * 100), static_cast<int16_t>(98 + row * 32), 92, 27};
      const bool active = chmodBits[col][row];
      drawButton(cell, rowLabels[row], active ? COLOR_ACCENT_DARK : COLOR_PANEL,
                 active ? COLOR_TEXT : COLOR_MUTED, 2, active ? COLOR_ACCENT : COLOR_BORDER);
    }
  }

  const uint16_t presets[] = {600, 644, 755, 777};
  for (uint8_t i = 0; i < 4; ++i) {
    Rect preset{static_cast<int16_t>(12 + i * 75), 197, 70, 25};
    char label[8];
    std::snprintf(label, sizeof(label), "%u", presets[i]);
    drawButton(preset, label, COLOR_RAISED, COLOR_TEXT, 2);
  }

  drawFooter("Toggle R/W/X, tap mode, or choose a preset");
}

size_t referencePageCount() {
  const size_t count = referenceShowsHttp ? HTTP_COUNT : PORT_COUNT;
  return (count + ITEMS_PER_REF_PAGE - 1) / ITEMS_PER_REF_PAGE;
}

void drawReference() {
  tft.fillScreen(COLOR_BG);
  drawHeader("QUICK REFERENCE");

  drawButton(REF_HTTP_TAB, "HTTP STATUS", referenceShowsHttp ? COLOR_ACCENT_DARK : COLOR_RAISED,
             COLOR_TEXT, 2, referenceShowsHttp ? COLOR_ACCENT : COLOR_BORDER);
  drawButton(REF_PORT_TAB, "COMMON PORTS", referenceShowsHttp ? COLOR_RAISED : COLOR_ACCENT_DARK,
             COLOR_TEXT, 2, referenceShowsHttp ? COLOR_BORDER : COLOR_ACCENT);

  const ReferenceItem* items = referenceShowsHttp ? HTTP_ITEMS : PORT_ITEMS;
  const size_t count = referenceShowsHttp ? HTTP_COUNT : PORT_COUNT;
  const size_t start = referencePage * ITEMS_PER_REF_PAGE;

  for (size_t i = 0; i < ITEMS_PER_REF_PAGE; ++i) {
    const size_t index = start + i;
    const int16_t y = 75 + static_cast<int16_t>(i * 25);
    Rect row{12, y, 296, 22};
    const uint16_t rowColor = i % 2 == 0 ? COLOR_PANEL : COLOR_PANEL_2;
    tft.fillRoundRect(row.x, row.y, row.w, row.h, 4, rowColor);
    if (index >= count) {
      continue;
    }

    tft.setTextColor(COLOR_ACCENT, rowColor);
    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(items[index].key, 20, y + 11);

    tft.setTextColor(COLOR_TEXT, rowColor);
    tft.setTextFont(1);
    tft.drawString(items[index].description, 78, y + 11);
  }

  drawButton(REF_PREV, "< PREV", referencePage > 0 ? COLOR_RAISED : COLOR_BG,
             referencePage > 0 ? COLOR_TEXT : COLOR_MUTED, 1);
  drawButton(REF_NEXT, "NEXT >", referencePage + 1 < referencePageCount() ? COLOR_RAISED : COLOR_BG,
             referencePage + 1 < referencePageCount() ? COLOR_TEXT : COLOR_MUTED, 1);

  char pageText[20];
  std::snprintf(pageText, sizeof(pageText), "%u / %u", static_cast<unsigned>(referencePage + 1),
                static_cast<unsigned>(referencePageCount()));
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(pageText, SCREEN_W / 2, 221);
}

uint8_t choiceCount() {
  switch (choiceDialog.target) {
    case ChoiceTarget::CronPreset:
      return CRON_PRESET_COUNT;
    case ChoiceTarget::BackupFrequency:
      return static_cast<uint8_t>(BACKUP_FREQ_COUNT);
    case ChoiceTarget::BackupScheme:
      return static_cast<uint8_t>(BACKUP_SCHEME_COUNT);
    default:
      return 0;
  }
}

const char* choiceLabel(uint8_t index) {
  switch (choiceDialog.target) {
    case ChoiceTarget::CronPreset:
      return cronPresetNameFor(index);
    case ChoiceTarget::BackupFrequency: {
      const char* labels[] = {"1 PER DAY", "2 PER DAY", "4 PER DAY", "HOURLY"};
      return labels[index];
    }
    case ChoiceTarget::BackupScheme: {
      const char* labels[] = {"7D / 4W / 12M", "14D / 8W / 12M", "GFS", "CUSTOM"};
      return labels[index];
    }
    default:
      return "";
  }
}

const char* choiceDetail(uint8_t index) {
  switch (choiceDialog.target) {
    case ChoiceTarget::CronPreset:
      return cronPresetDetail(index);
    case ChoiceTarget::BackupFrequency: {
      const char* details[] = {"every 24 hours", "every 12 hours", "every 6 hours", "every hour"};
      return details[index];
    }
    case ChoiceTarget::BackupScheme: {
      const char* details[] = {"standard retention", "longer retention",
                               "grandfather-father-son", "set D / W / M"};
      return details[index];
    }
    default:
      return "";
  }
}

bool choiceSelected(uint8_t index) {
  switch (choiceDialog.target) {
    case ChoiceTarget::CronPreset:
      return !cronCustom && cronPreset == index;
    case ChoiceTarget::BackupFrequency:
      return backupFrequencyIndex == index;
    case ChoiceTarget::BackupScheme:
      return backupSchemeIndex == index;
    default:
      return false;
  }
}

Rect choiceRect(uint8_t index) {
  const uint8_t col = index % 2;
  const uint8_t row = index / 2;
  return {static_cast<int16_t>(12 + col * 151), static_cast<int16_t>(47 + row * 43), 145, 38};
}

void openChoiceDialog(ChoiceTarget target, const char* title) {
  choiceDialog.active = true;
  choiceDialog.target = target;
  std::strncpy(choiceDialog.title, title, sizeof(choiceDialog.title) - 1);
  choiceDialog.title[sizeof(choiceDialog.title) - 1] = '\0';
}

void drawChoiceDialog() {
  tft.fillScreen(COLOR_BG);
  drawHeader(choiceDialog.title, true, "BACK");

  const uint8_t count = choiceCount();
  for (uint8_t i = 0; i < count; ++i) {
    const Rect option = choiceRect(i);
    const bool selected = choiceSelected(i);
    const uint16_t bg = selected ? COLOR_ACCENT_DARK : COLOR_PANEL;
    drawRoundedPanel(option, bg, selected ? COLOR_ACCENT : COLOR_BORDER, 7);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT, bg);
    tft.drawString(choiceLabel(i), option.x + option.w / 2, option.y + 12);
    tft.setTextColor(selected ? COLOR_ACCENT : COLOR_MUTED, bg);
    tft.drawString(choiceDetail(i), option.x + option.w / 2, option.y + 27);
  }

  drawFooter("Tap one option to apply it");
}

void disableBle() {
  if (bleScanner != nullptr && bleScanner->isScanning()) {
    bleScanner->stop();
  }
  if (NimBLEDevice::getInitialized()) {
    NimBLEDevice::deinit(true);
  }
  bleScanner = nullptr;
  bleEnabled = false;
  bleCount = 0;
  bleTotalFound = 0;
  wirelessStatus = "BLE radio off";
}

void disableWifi() {
  if (!wifiEnabled) {
    return;
  }
  WiFi.scanDelete();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  wifiEnabled = false;
  wifiCount = 0;
  wifiTotalFound = 0;
  wifiMonitorHasTarget = false;
  std::memset(wifiChannelCount, 0, sizeof(wifiChannelCount));
  wirelessStatus = "WiFi radio off";
}

void enableWifi() {
  disableBle();
  wirelessTab = WirelessTab::Wifi;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  delay(80);
  wifiEnabled = true;
  wirelessStatus = "WiFi ready - tap SCAN";
}

void enableBle() {
  disableWifi();
  wirelessTab = WirelessTab::Ble;
  NimBLEDevice::init("");
  bleScanner = NimBLEDevice::getScan();
  if (bleScanner != nullptr) {
    bleScanner->setActiveScan(bleActiveScan);
    bleScanner->setInterval(90);
    bleScanner->setWindow(60);
    bleScanner->setMaxResults(40);
    bleEnabled = true;
    wirelessStatus = "BLE ready - tap SCAN";
  } else {
    bleEnabled = false;
    wirelessStatus = "BLE initialization failed";
  }
}

void sortWifiRows() {
  for (uint8_t i = 0; i < wifiCount; ++i) {
    for (uint8_t j = i + 1; j < wifiCount; ++j) {
      if (wifiRows[j].rssi > wifiRows[i].rssi) {
        WifiRow tmp = wifiRows[i];
        wifiRows[i] = wifiRows[j];
        wifiRows[j] = tmp;
      }
    }
  }
}

void sortBleRows() {
  for (uint8_t i = 0; i < bleCount; ++i) {
    for (uint8_t j = i + 1; j < bleCount; ++j) {
      if (bleRows[j].rssi > bleRows[i].rssi) {
        BleRow tmp = bleRows[i];
        bleRows[i] = bleRows[j];
        bleRows[j] = tmp;
      }
    }
  }
}

void drawWirelessScanningMessage(const char* message) {
  tft.fillRect(0, 100, SCREEN_W, 104, COLOR_BG);
  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_WARNING, COLOR_BG);
  tft.drawString(message, SCREEN_W / 2, 140);
  tft.setTextFont(1);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString("The scan may take a few seconds", SCREEN_W / 2, 160);
}

void scanWifiNetworks() {
  if (!wifiEnabled) {
    enableWifi();
  }
  drawWirelessScanningMessage(wifiPassiveScan ? "Passive WiFi scan..." : "Active WiFi scan...");

  std::memset(wifiChannelCount, 0, sizeof(wifiChannelCount));
  for (uint8_t i = 0; i < 15; ++i) {
    wifiChannelBestRssi[i] = -1000;
  }

  WiFi.scanDelete();
  const uint32_t dwellMs = wifiPassiveScan ? 450U : 180U;
  const int found = WiFi.scanNetworks(false, true, wifiPassiveScan, dwellMs);
  wifiTotalFound = found < 0 ? 0 : static_cast<uint16_t>(found);
  wifiCount = 0;

  if (found > 0) {
    for (int i = 0; i < found; ++i) {
      const int32_t channel = WiFi.channel(i);
      if (channel >= 1 && channel <= 14) {
        ++wifiChannelCount[channel];
        if (WiFi.RSSI(i) > wifiChannelBestRssi[channel]) {
          wifiChannelBestRssi[channel] = WiFi.RSSI(i);
        }
      }

      if (wifiCount < MAX_WIFI_ROWS) {
        WifiRow& row = wifiRows[wifiCount];
        row.ssid = WiFi.SSID(i);
        if (row.ssid.length() == 0) {
          row.ssid = "<hidden>";
        }
        row.bssid = WiFi.BSSIDstr(i);
        const uint8_t* bssid = WiFi.BSSID(i);
        for (uint8_t byteIndex = 0; byteIndex < 6; ++byteIndex) {
          row.bssidBytes[byteIndex] = bssid == nullptr ? 0 : bssid[byteIndex];
        }
        row.rssi = WiFi.RSSI(i);
        row.channel = channel;
        row.auth = WiFi.encryptionType(i);
        ++wifiCount;
      }
    }
  }

  sortWifiRows();
  wifiPage = 0;
  if (found < 0) {
    wirelessStatus = "WiFi scan failed - try again";
  } else if (found == 0) {
    wirelessStatus = "No WiFi networks found";
  } else {
    wirelessStatus = "Tap a network to monitor its signal";
  }
  WiFi.scanDelete();
}

void scanBleDevices() {
  if (!bleEnabled) {
    enableBle();
  }
  drawWirelessScanningMessage(bleActiveScan ? "Active BLE scan..." : "Passive BLE scan...");

  bleCount = 0;
  bleTotalFound = 0;
  if (bleScanner == nullptr) {
    wirelessStatus = "BLE scanner is unavailable";
    return;
  }

  bleScanner->stop();
  bleScanner->clearResults();
  bleScanner->setActiveScan(bleActiveScan);
  NimBLEScanResults results = bleScanner->start(4, false);
  const int resultCount = results.getCount();
  bleTotalFound = resultCount < 0 ? 0 : static_cast<uint16_t>(resultCount);

  for (int i = 0; i < resultCount && bleCount < MAX_BLE_ROWS; ++i) {
    NimBLEAdvertisedDevice device = results.getDevice(i);
    String name = device.getName().c_str();
    if (name.length() == 0) {
      name = "<no name>";
    }
    bleRows[bleCount] = {name, device.getAddress().toString().c_str(), device.getRSSI()};
    ++bleCount;
  }

  bleScanner->clearResults();
  sortBleRows();
  blePage = 0;
  if (resultCount == 0) {
    wirelessStatus = bleActiveScan ? "No BLE devices - try PASSIVE or move closer"
                                   : "No BLE devices - try ACTIVE scan";
  } else {
    wirelessStatus = "BLE scan complete";
  }
}

uint8_t wifiBestChannel() {
  const uint8_t candidates[3] = {1, 6, 11};
  uint8_t best = candidates[0];
  for (uint8_t i = 1; i < 3; ++i) {
    const uint8_t channel = candidates[i];
    if (wifiChannelCount[channel] < wifiChannelCount[best]) {
      best = channel;
    } else if (wifiChannelCount[channel] == wifiChannelCount[best] &&
               wifiChannelBestRssi[channel] < wifiChannelBestRssi[best]) {
      best = channel;
    }
  }
  return best;
}

void drawWifiChannelView() {
  char summary[64];
  std::snprintf(summary, sizeof(summary), "%u networks. Suggested 2.4GHz CH %u",
                static_cast<unsigned>(wifiTotalFound), static_cast<unsigned>(wifiBestChannel()));
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_SUCCESS, COLOR_BG);
  tft.drawString(summary, 12, 105);

  for (uint8_t channel = 1; channel <= 13; ++channel) {
    const int16_t x = 12 + ((channel - 1) % 7) * 43;
    const int16_t y = 128 + ((channel - 1) / 7) * 36;
    Rect cell{x, y, 38, 28};
    const bool common = channel == 1 || channel == 6 || channel == 11;
    drawRoundedPanel(cell, common ? COLOR_PANEL : COLOR_PANEL_2, common ? COLOR_ACCENT : COLOR_BORDER, 4);
    char label[12];
    std::snprintf(label, sizeof(label), "%u:%u", static_cast<unsigned>(channel),
                  static_cast<unsigned>(wifiChannelCount[channel]));
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(common ? COLOR_ACCENT : COLOR_TEXT, common ? COLOR_PANEL : COLOR_PANEL_2);
    tft.drawString(label, cell.x + cell.w / 2, cell.y + cell.h / 2);
  }
}

Rect wifiListRowRect(uint8_t visibleRow) {
  return {12, static_cast<int16_t>(103 + visibleRow * 25), 296, 23};
}

void drawWifiList() {
  const uint8_t start = wifiPage * ITEMS_PER_WIRELESS_PAGE;
  for (uint8_t i = 0; i < ITEMS_PER_WIRELESS_PAGE; ++i) {
    const uint8_t index = start + i;
    const Rect row = wifiListRowRect(i);
    const uint16_t bg = i % 2 == 0 ? COLOR_PANEL : COLOR_PANEL_2;
    tft.fillRoundRect(row.x, row.y, row.w, row.h, 4, bg);
    if (index >= wifiCount) {
      continue;
    }

    tft.setTextFont(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_TEXT, bg);
    tft.drawString(trimForDisplay(wifiRows[index].ssid, 17), 18, row.y + 8);
    char detail[34];
    std::snprintf(detail, sizeof(detail), "CH%ld %s %ddBm", static_cast<long>(wifiRows[index].channel),
                  wifiAuthName(wifiRows[index].auth).c_str(), static_cast<int>(wifiRows[index].rssi));
    tft.setTextColor(COLOR_MUTED, bg);
    tft.drawString(detail, 18, row.y + 18);
    drawSignalBars(222, row.y + 4, wifiRows[index].rssi, bg);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(signalColor(wifiRows[index].rssi), bg);
    tft.drawString(signalLabel(wifiRows[index].rssi), 300, row.y + 13);
  }
}

void drawBleList() {
  const uint8_t start = blePage * ITEMS_PER_WIRELESS_PAGE;
  for (uint8_t i = 0; i < ITEMS_PER_WIRELESS_PAGE; ++i) {
    const uint8_t index = start + i;
    const int16_t y = 103 + static_cast<int16_t>(i * 25);
    Rect row{12, y, 296, 23};
    const uint16_t bg = i % 2 == 0 ? COLOR_PANEL : COLOR_PANEL_2;
    tft.fillRoundRect(row.x, row.y, row.w, row.h, 4, bg);
    if (index >= bleCount) {
      continue;
    }

    tft.setTextFont(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_TEXT, bg);
    tft.drawString(trimForDisplay(bleRows[index].name, 18), 18, y + 8);
    tft.setTextColor(COLOR_MUTED, bg);
    tft.drawString(trimForDisplay(bleRows[index].address, 22), 18, y + 18);
    drawSignalBars(222, y + 4, bleRows[index].rssi, bg);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(signalColor(bleRows[index].rssi), bg);
    tft.drawString(signalLabel(bleRows[index].rssi), 300, y + 13);
  }
}

void drawWirelessStatusMessage(const char* primary, const char* secondary) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString(primary, SCREEN_W / 2, 135);
  tft.setTextFont(1);
  tft.drawString(secondary, SCREEN_W / 2, 158);
}

void drawWireless() {
  tft.fillScreen(COLOR_BG);
  drawHeader("WIRELESS SCANNER");

  char wifiLabel[18];
  std::snprintf(wifiLabel, sizeof(wifiLabel), "WiFi %s", wifiEnabled ? "ON" : "OFF");
  char bleLabel[18];
  std::snprintf(bleLabel, sizeof(bleLabel), "BLE %s", bleEnabled ? "ON" : "OFF");
  drawButton(WIRELESS_WIFI_TOGGLE, wifiLabel,
             wirelessTab == WirelessTab::Wifi ? (wifiEnabled ? COLOR_SUCCESS : COLOR_ACCENT_DARK) : COLOR_RAISED,
             COLOR_TEXT, 2, wirelessTab == WirelessTab::Wifi ? COLOR_ACCENT : COLOR_BORDER);
  drawButton(WIRELESS_BLE_TOGGLE, bleLabel,
             wirelessTab == WirelessTab::Ble ? (bleEnabled ? COLOR_SUCCESS : COLOR_ACCENT_DARK) : COLOR_RAISED,
             COLOR_TEXT, 2, wirelessTab == WirelessTab::Ble ? COLOR_ACCENT : COLOR_BORDER);

  const bool radioOn = wirelessTab == WirelessTab::Wifi ? wifiEnabled : bleEnabled;
  drawButton(WIRELESS_SCAN, "SCAN", radioOn ? COLOR_RAISED : COLOR_PANEL,
             radioOn ? COLOR_TEXT : COLOR_MUTED, 1);

  const char* modeLabel = wirelessTab == WirelessTab::Wifi
                              ? (wifiPassiveScan ? "PASSIVE" : "ACTIVE")
                              : (bleActiveScan ? "ACTIVE" : "PASSIVE");
  drawButton(WIRELESS_MODE, modeLabel, radioOn ? COLOR_RAISED : COLOR_PANEL,
             radioOn ? COLOR_TEXT : COLOR_MUTED, 1);

  if (wirelessTab == WirelessTab::Wifi) {
    drawButton(WIRELESS_VIEW, wifiChannelView ? "NETWORKS" : "CHANNELS",
               radioOn ? COLOR_RAISED : COLOR_PANEL, radioOn ? COLOR_TEXT : COLOR_MUTED, 1);
  } else {
    drawButton(WIRELESS_VIEW, "CLEAR", radioOn && bleCount > 0 ? COLOR_RAISED : COLOR_PANEL,
               radioOn && bleCount > 0 ? COLOR_TEXT : COLOR_MUTED, 1);
  }

  if (wirelessTab == WirelessTab::Wifi) {
    if (!wifiEnabled) {
      drawWirelessStatusMessage("Tap WiFi OFF to enable", "No password or configuration is needed");
    } else if (wifiCount == 0 && wifiTotalFound == 0) {
      drawWirelessStatusMessage(wirelessStatus.c_str(), "ACTIVE is faster; PASSIVE only listens");
    } else if (wifiChannelView) {
      drawWifiChannelView();
    } else {
      drawWifiList();
    }

    const uint8_t pages = (wifiCount + ITEMS_PER_WIRELESS_PAGE - 1) / ITEMS_PER_WIRELESS_PAGE;
    drawButton(WIRELESS_PREV, "< PREV", (!wifiChannelView && wifiPage > 0) ? COLOR_RAISED : COLOR_BG,
               (!wifiChannelView && wifiPage > 0) ? COLOR_TEXT : COLOR_MUTED, 1);
    drawButton(WIRELESS_NEXT, "NEXT >", (!wifiChannelView && wifiPage + 1 < pages) ? COLOR_RAISED : COLOR_BG,
               (!wifiChannelView && wifiPage + 1 < pages) ? COLOR_TEXT : COLOR_MUTED, 1);
  } else {
    if (!bleEnabled) {
      drawWirelessStatusMessage("Tap BLE OFF to enable", "ACTIVE usually retrieves more device names");
    } else if (bleCount == 0 && bleTotalFound == 0) {
      drawWirelessStatusMessage(wirelessStatus.c_str(), "Switch ACTIVE / PASSIVE and scan again");
    } else {
      drawBleList();
    }

    const uint8_t pages = (bleCount + ITEMS_PER_WIRELESS_PAGE - 1) / ITEMS_PER_WIRELESS_PAGE;
    drawButton(WIRELESS_PREV, "< PREV", blePage > 0 ? COLOR_RAISED : COLOR_BG,
               blePage > 0 ? COLOR_TEXT : COLOR_MUTED, 1);
    drawButton(WIRELESS_NEXT, "NEXT >", blePage + 1 < pages ? COLOR_RAISED : COLOR_BG,
               blePage + 1 < pages ? COLOR_TEXT : COLOR_MUTED, 1);
  }
}

void pushWifiMonitorSample(int16_t rssi) {
  if (wifiMonitorHistoryCount < sizeof(wifiMonitorHistory) / sizeof(wifiMonitorHistory[0])) {
    wifiMonitorHistory[wifiMonitorHistoryCount++] = rssi;
    return;
  }
  for (uint8_t i = 1; i < wifiMonitorHistoryCount; ++i) {
    wifiMonitorHistory[i - 1] = wifiMonitorHistory[i];
  }
  wifiMonitorHistory[wifiMonitorHistoryCount - 1] = rssi;
}

void drawLargeSignalBar(const Rect& rect, int rssi, bool found) {
  drawRoundedPanel(rect, COLOR_PANEL, COLOR_BORDER, 7);
  const uint8_t percent = found ? signalPercent(rssi) : 0;
  const int16_t innerWidth = rect.w - 6;
  const int16_t filled = static_cast<int16_t>((static_cast<uint32_t>(innerWidth) * percent) / 100U);
  if (filled > 0) {
    tft.fillRoundRect(rect.x + 3, rect.y + 3, filled, rect.h - 6, 4, signalColor(rssi));
  }
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, filled > rect.w / 2 ? signalColor(rssi) : COLOR_PANEL);
  tft.drawString(found ? String(percent) + "%" : "NO SIGNAL", rect.x + rect.w / 2, rect.y + rect.h / 2 + 1);
}

void drawWifiMonitorHistory() {
  const Rect graph{20, 162, 280, 35};
  drawRoundedPanel(graph, COLOR_PANEL_2, COLOR_BORDER, 5);
  tft.drawFastHLine(graph.x + 4, graph.y + 17, graph.w - 8, COLOR_BORDER);

  if (wifiMonitorHistoryCount < 2) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(COLOR_MUTED, COLOR_PANEL_2);
    tft.drawString("Signal history appears after two scans", graph.x + graph.w / 2, graph.y + 18);
    return;
  }

  const int16_t usableWidth = graph.w - 10;
  for (uint8_t i = 1; i < wifiMonitorHistoryCount; ++i) {
    const int16_t previous = clampInt32(wifiMonitorHistory[i - 1], -95, -35);
    const int16_t current = clampInt32(wifiMonitorHistory[i], -95, -35);
    const int16_t x1 = graph.x + 5 + static_cast<int16_t>((static_cast<uint32_t>(i - 1) * usableWidth) / 35U);
    const int16_t x2 = graph.x + 5 + static_cast<int16_t>((static_cast<uint32_t>(i) * usableWidth) / 35U);
    const int16_t y1 = graph.y + graph.h - 5 - static_cast<int16_t>(((previous + 95) * (graph.h - 10)) / 60);
    const int16_t y2 = graph.y + graph.h - 5 - static_cast<int16_t>(((current + 95) * (graph.h - 10)) / 60);
    tft.drawLine(x1, y1, x2, y2, signalColor(current));
  }
}

void drawWifiMonitorDynamic() {
  tft.fillRect(0, 91, SCREEN_W, 109, COLOR_BG);

  char value[24];
  if (wifiMonitorFound) {
    std::snprintf(value, sizeof(value), "%d dBm", static_cast<int>(wifiMonitorRssi));
  } else {
    std::snprintf(value, sizeof(value), "not found");
  }
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(wifiMonitorFound ? signalColor(wifiMonitorRssi) : COLOR_DANGER, COLOR_BG);
  tft.drawString(value, 20, 108);
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(2);
  tft.drawString(wifiMonitorFound ? signalLabel(wifiMonitorRssi) : "Out of range", 300, 108);

  drawLargeSignalBar({20, 124, 280, 28}, wifiMonitorRssi, wifiMonitorFound);
  drawWifiMonitorHistory();
}

void drawWifiMonitor() {
  tft.fillScreen(COLOR_BG);
  drawHeader("WIFI SIGNAL MONITOR", true, "LIST");

  Rect network{12, 42, 296, 43};
  drawRoundedPanel(network, COLOR_PANEL, COLOR_ACCENT, 7);
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawString(trimForDisplay(monitoredWifi.ssid, 24), 20, 56);
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(COLOR_ACCENT, COLOR_PANEL);
  tft.drawString("LIVE ~2s", 300, 56);
  char detail[64];
  std::snprintf(detail, sizeof(detail), "CH%ld  %s  %s", static_cast<long>(monitoredWifi.channel),
                wifiAuthName(monitoredWifi.auth).c_str(), monitoredWifi.bssid.c_str());
  tft.setTextDatum(ML_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.drawString(trimForDisplay(String(detail), 47), 20, 74);

  drawWifiMonitorDynamic();
  drawButton(WIFI_MONITOR_RESCAN, "SCAN NOW", COLOR_RAISED, COLOR_TEXT, 1);
  drawButton(WIFI_MONITOR_LIST, "BACK TO LIST", COLOR_RAISED, COLOR_TEXT, 1);
}

void startWifiMonitor(uint8_t index) {
  if (index >= wifiCount) {
    return;
  }
  monitoredWifi = wifiRows[index];
  wifiMonitorHasTarget = true;
  wifiMonitorFound = true;
  wifiMonitorRssi = static_cast<int16_t>(monitoredWifi.rssi);
  wifiMonitorHistoryCount = 0;
  pushWifiMonitorSample(wifiMonitorRssi);
  lastWifiMonitorAt = 0;
  currentScreen = Screen::WifiMonitor;
  drawWifiMonitor();
}

void scanMonitoredWifi() {
  if (!wifiEnabled || !wifiMonitorHasTarget) {
    wifiMonitorFound = false;
    wifiMonitorRssi = -100;
    pushWifiMonitorSample(wifiMonitorRssi);
    lastWifiMonitorAt = millis();
    return;
  }

  WiFi.scanDelete();
  const uint32_t dwellMs = wifiPassiveScan ? 350U : 140U;
  const int found = WiFi.scanNetworks(false, true, wifiPassiveScan, dwellMs,
                                      static_cast<uint8_t>(monitoredWifi.channel), nullptr,
                                      monitoredWifi.bssidBytes);
  wifiMonitorFound = found > 0;
  wifiMonitorRssi = wifiMonitorFound ? static_cast<int16_t>(WiFi.RSSI(0)) : -100;
  if (wifiMonitorFound) {
    monitoredWifi.rssi = wifiMonitorRssi;
  }
  pushWifiMonitorSample(wifiMonitorRssi);
  lastWifiMonitorAt = millis();
  WiFi.scanDelete();
}

void applyNumberEditorValue(uint32_t rawValue) {
  const uint32_t value = static_cast<uint32_t>(clampInt32(static_cast<int32_t>(rawValue),
                                                          static_cast<int32_t>(numberEditor.minValue),
                                                          static_cast<int32_t>(numberEditor.maxValue)));
  switch (numberEditor.target) {
    case NumberTarget::CidrOctet0:
    case NumberTarget::CidrOctet1:
    case NumberTarget::CidrOctet2:
    case NumberTarget::CidrOctet3: {
      const uint8_t index = static_cast<uint8_t>(numberEditor.target) - static_cast<uint8_t>(NumberTarget::CidrOctet0);
      cidrOctets[index] = static_cast<uint8_t>(value);
      break;
    }
    case NumberTarget::CidrPrefix:
      cidrPrefix = static_cast<uint8_t>(value);
      break;
    case NumberTarget::VlsmLan1:
    case NumberTarget::VlsmLan2:
    case NumberTarget::VlsmLan3:
    case NumberTarget::VlsmLan4: {
      const uint8_t index = static_cast<uint8_t>(numberEditor.target) - static_cast<uint8_t>(NumberTarget::VlsmLan1);
      vlsmHosts[index] = static_cast<uint16_t>(value);
      vlsmSelected = index;
      break;
    }
    case NumberTarget::ClockHhmm:
      setSelectedClockFromHhmm(value);
      break;
    case NumberTarget::CronInterval:
      cronIntervalMinutes = static_cast<uint8_t>(value);
      break;
    case NumberTarget::CronHour:
      cronHour = static_cast<uint8_t>(value);
      break;
    case NumberTarget::CronMinute:
      cronMinute = static_cast<uint8_t>(value);
      break;
    case NumberTarget::StorageDaily:
      storageDailyGb = static_cast<uint16_t>(value);
      break;
    case NumberTarget::StorageRetention:
      storageRetentionDays = static_cast<uint16_t>(value);
      break;
    case NumberTarget::StorageCompression:
      storageCompressionPct = static_cast<uint8_t>(value);
      break;
    case NumberTarget::StorageCopies:
      storageCopies = static_cast<uint8_t>(value);
      break;
    case NumberTarget::StorageSpare:
      storageSparePct = static_cast<uint8_t>(value);
      break;
    case NumberTarget::BackupFull:
      backupFullGb = static_cast<uint16_t>(value);
      break;
    case NumberTarget::BackupIncremental:
      backupIncrementalGb = static_cast<uint16_t>(value);
      break;
    case NumberTarget::BackupCopies:
      backupCopies = static_cast<uint8_t>(value);
      break;
    case NumberTarget::BackupDaily:
      backupCustomDaily = static_cast<uint8_t>(value);
      break;
    case NumberTarget::BackupWeekly:
      backupCustomWeekly = static_cast<uint8_t>(value);
      break;
    case NumberTarget::BackupMonthly:
      backupCustomMonthly = static_cast<uint8_t>(value);
      break;
    case NumberTarget::ChmodMode:
      applyChmodPreset(static_cast<uint16_t>(value));
      break;
    default:
      break;
  }
}

void startNumberEditor(NumberTarget target, const char* title, uint32_t value, uint32_t minValue,
                       uint32_t maxValue, const char* unit, uint8_t maxDigits, bool zeroPad) {
  numberEditor.active = true;
  numberEditor.target = target;
  numberEditor.minValue = minValue;
  numberEditor.maxValue = maxValue;
  numberEditor.currentValue = value;
  numberEditor.maxDigits = maxDigits;
  numberEditor.zeroPad = zeroPad;
  std::strncpy(numberEditor.title, title, sizeof(numberEditor.title) - 1);
  numberEditor.title[sizeof(numberEditor.title) - 1] = '\0';
  std::strncpy(numberEditor.unit, unit, sizeof(numberEditor.unit) - 1);
  numberEditor.unit[sizeof(numberEditor.unit) - 1] = '\0';
  if (zeroPad) {
    char format[8];
    std::snprintf(format, sizeof(format), "%%0%ulu", static_cast<unsigned>(maxDigits));
    std::snprintf(numberEditor.digits, sizeof(numberEditor.digits), format, static_cast<unsigned long>(value));
  } else {
    std::snprintf(numberEditor.digits, sizeof(numberEditor.digits), "%lu", static_cast<unsigned long>(value));
  }
}

String numberEditorDisplayValue() {
  if (numberEditor.target == NumberTarget::ClockHhmm) {
    const uint32_t raw = static_cast<uint32_t>(std::strtoul(numberEditor.digits, nullptr, 10));
    if (std::strlen(numberEditor.digits) == 4) {
      return formatHhmmRaw(raw);
    }
    return String(numberEditor.digits) + "  HHMM";
  }
  String result(numberEditor.digits);
  if (std::strlen(numberEditor.unit) > 0) {
    result += " ";
    result += numberEditor.unit;
  }
  return result;
}

Rect numberKeyRect(uint8_t index) {
  const uint8_t col = index % 4;
  const uint8_t row = index / 4;
  return {static_cast<int16_t>(12 + col * 77), static_cast<int16_t>(92 + row * 35), 68, 30};
}

void drawNumberEditor() {
  tft.fillScreen(COLOR_BG);
  drawHeader("TYPE VALUE", false);

  tft.setTextFont(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString(numberEditor.title, SCREEN_W / 2, 49);

  Rect display{12, 61, 296, 26};
  drawRoundedPanel(display, COLOR_PANEL);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_ACCENT, COLOR_PANEL);
  tft.drawString(numberEditorDisplayValue(), SCREEN_W / 2, 75);

  const char* labels[16] = {"1", "2", "3", "DEL", "4", "5", "6", "CLR",
                            "7", "8", "9", "OK", "CANCEL", "0", "MIN", "MAX"};
  for (uint8_t i = 0; i < 16; ++i) {
    uint16_t fill = COLOR_RAISED;
    uint16_t text = COLOR_TEXT;
    if (std::strcmp(labels[i], "OK") == 0) {
      fill = COLOR_SUCCESS;
    } else if (std::strcmp(labels[i], "CANCEL") == 0) {
      fill = COLOR_DANGER;
    } else if (std::strcmp(labels[i], "MIN") == 0 || std::strcmp(labels[i], "MAX") == 0) {
      fill = COLOR_PANEL;
      text = COLOR_MUTED;
    }
    drawButton(numberKeyRect(i), labels[i], fill, text, 1);
  }

  char range[42];
  std::snprintf(range, sizeof(range), "Range: %lu - %lu", static_cast<unsigned long>(numberEditor.minValue),
                static_cast<unsigned long>(numberEditor.maxValue));
  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.drawString(range, SCREEN_W / 2, 232);
}

void acceptNumberEditor() {
  uint32_t raw = 0;
  if (std::strlen(numberEditor.digits) > 0) {
    raw = static_cast<uint32_t>(std::strtoul(numberEditor.digits, nullptr, 10));
  }
  applyNumberEditorValue(raw);
  numberEditor.active = false;
  drawCurrentScreen();
}

void handleNumberEditorTouch(uint16_t x, uint16_t y) {
  for (uint8_t i = 0; i < 16; ++i) {
    if (!contains(numberKeyRect(i), x, y)) {
      continue;
    }
    const char* labels[16] = {"1", "2", "3", "DEL", "4", "5", "6", "CLR",
                              "7", "8", "9", "OK", "CANCEL", "0", "MIN", "MAX"};
    const char* label = labels[i];
    const size_t len = std::strlen(numberEditor.digits);
    if (std::strcmp(label, "OK") == 0) {
      acceptNumberEditor();
      return;
    }
    if (std::strcmp(label, "CANCEL") == 0) {
      numberEditor.active = false;
      drawCurrentScreen();
      return;
    }
    if (std::strcmp(label, "DEL") == 0) {
      if (len > 0) {
        numberEditor.digits[len - 1] = '\0';
      }
      drawNumberEditor();
      return;
    }
    if (std::strcmp(label, "CLR") == 0) {
      numberEditor.digits[0] = '\0';
      drawNumberEditor();
      return;
    }
    if (std::strcmp(label, "MIN") == 0) {
      std::snprintf(numberEditor.digits, sizeof(numberEditor.digits), "%lu",
                    static_cast<unsigned long>(numberEditor.minValue));
      drawNumberEditor();
      return;
    }
    if (std::strcmp(label, "MAX") == 0) {
      std::snprintf(numberEditor.digits, sizeof(numberEditor.digits), "%lu",
                    static_cast<unsigned long>(numberEditor.maxValue));
      drawNumberEditor();
      return;
    }
    if (label[0] >= '0' && label[0] <= '9' && len < numberEditor.maxDigits) {
      numberEditor.digits[len] = label[0];
      numberEditor.digits[len + 1] = '\0';
      drawNumberEditor();
      return;
    }
  }
}

void drawCurrentScreen() {
  if (numberEditor.active) {
    drawNumberEditor();
    return;
  }
  if (choiceDialog.active) {
    drawChoiceDialog();
    return;
  }
  switch (currentScreen) {
    case Screen::Home:
      drawHome();
      break;
    case Screen::Cidr:
      drawCidr();
      break;
    case Screen::Vlsm:
      drawVlsm();
      break;
    case Screen::Clock:
      drawClock();
      break;
    case Screen::Cron:
      drawCron();
      break;
    case Screen::CronEdit:
      drawCronEdit();
      break;
    case Screen::Capacity:
      drawCapacity();
      break;
    case Screen::BackupRetention:
      drawBackupRetention();
      break;
    case Screen::Chmod:
      drawChmod();
      break;
    case Screen::Wireless:
      drawWireless();
      break;
    case Screen::WifiMonitor:
      drawWifiMonitor();
      break;
    case Screen::Reference:
      drawReference();
      break;
  }
}

void goHome() {
  choiceDialog.active = false;
  choiceDialog.target = ChoiceTarget::None;
  cidrReturnScreen = Screen::Home;
  currentScreen = Screen::Home;
  drawHome();
}

void handleHomeTouch(uint16_t x, uint16_t y) {
  for (uint8_t i = 0; i < HOME_TOOL_COUNT; ++i) {
    const uint8_t col = i % 4;
    const uint8_t row = i / 4;
    Rect card{static_cast<int16_t>(8 + col * 78), static_cast<int16_t>(44 + row * 82), 72, 72};
    if (contains(card, x, y)) {
      currentScreen = HOME_TOOLS[i].screen;
      if (currentScreen == Screen::Cidr) {
        cidrReturnScreen = Screen::Home;
      }
      drawCurrentScreen();
      return;
    }
  }
}

void handleCidrTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    if (cidrReturnScreen == Screen::Vlsm) {
      currentScreen = Screen::Vlsm;
      cidrReturnScreen = Screen::Home;
      drawVlsm();
    } else {
      goHome();
    }
    return;
  }
  for (uint8_t i = 0; i < 4; ++i) {
    Rect box{static_cast<int16_t>(12 + i * 53), 56, 45, 31};
    if (contains(box, x, y)) {
      char title[24];
      std::snprintf(title, sizeof(title), "IPv4 octet %u", static_cast<unsigned>(i + 1));
      startNumberEditor(static_cast<NumberTarget>(static_cast<uint8_t>(NumberTarget::CidrOctet0) + i),
                        title, cidrOctets[i], 0, 255, "", 3);
      drawNumberEditor();
      return;
    }
  }
  Rect prefixBox{238, 56, 70, 31};
  if (contains(prefixBox, x, y)) {
    startNumberEditor(NumberTarget::CidrPrefix, "CIDR prefix", cidrPrefix, 0, 32, "", 2);
    drawNumberEditor();
    return;
  }
  if (contains(CIDR_PREFIX_MINUS, x, y) && cidrPrefix > 0) {
    --cidrPrefix;
    drawCidr();
    return;
  }
  if (contains(CIDR_PREFIX_PLUS, x, y) && cidrPrefix < 32) {
    ++cidrPrefix;
    drawCidr();
  }
}

void handleVlsmTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    goHome();
    return;
  }
  if (contains(VLSM_BASE_BUTTON, x, y)) {
    cidrReturnScreen = Screen::Vlsm;
    currentScreen = Screen::Cidr;
    drawCidr();
    return;
  }
  for (uint8_t lan = 0; lan < 4; ++lan) {
    const int16_t yRow = 70 + static_cast<int16_t>(lan) * 32;
    Rect minus{12, yRow, 28, 28};
    Rect center{45, yRow, 160, 28};
    Rect plus{210, yRow, 28, 28};
    Rect prefix{244, yRow, 64, 28};
    if (contains(minus, x, y)) {
      const uint16_t step = stepForHostValue(vlsmHosts[lan]);
      vlsmHosts[lan] = static_cast<uint16_t>(clampInt32(static_cast<int32_t>(vlsmHosts[lan]) - step, 1, 16000));
      vlsmSelected = lan;
      drawVlsm();
      return;
    }
    if (contains(plus, x, y)) {
      const uint16_t step = stepForHostValue(vlsmHosts[lan]);
      vlsmHosts[lan] = static_cast<uint16_t>(clampInt32(static_cast<int32_t>(vlsmHosts[lan]) + step, 1, 16000));
      vlsmSelected = lan;
      drawVlsm();
      return;
    }
    if (contains(center, x, y)) {
      char title[24];
      std::snprintf(title, sizeof(title), "LAN%u hosts", static_cast<unsigned>(lan + 1));
      startNumberEditor(static_cast<NumberTarget>(static_cast<uint8_t>(NumberTarget::VlsmLan1) + lan),
                        title, vlsmHosts[lan], 1, 16000, "hosts", 5);
      drawNumberEditor();
      return;
    }
    if (contains(prefix, x, y)) {
      vlsmSelected = lan;
      drawVlsm();
      return;
    }
  }
}

void handleClockTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    goHome();
    return;
  }
  for (uint8_t i = 0; i < 4; ++i) {
    Rect row{12, static_cast<int16_t>(54 + i * 29), 296, 27};
    if (contains(row, x, y)) {
      selectedClock = i;
      drawClock();
      return;
    }
  }
  if (contains(CLOCK_SET_BUTTON, x, y)) {
    char title[28];
    std::snprintf(title, sizeof(title), "Set %s time", nameForClock(selectedClock));
    startNumberEditor(NumberTarget::ClockHhmm, title, selectedClockHhmmValue(), 0, 2359, "HHMM", 4, true);
    drawNumberEditor();
    return;
  }
  if (selectedClock == 3 && contains(CLOCK_TZ_MINUS, x, y)) {
    if (customClockOffset > -12) {
      --customClockOffset;
      lastClockRenderedMinute = -1;
    }
    drawClock();
    return;
  }
  if (selectedClock == 3 && contains(CLOCK_TZ_PLUS, x, y)) {
    if (customClockOffset < 14) {
      ++customClockOffset;
      lastClockRenderedMinute = -1;
    }
    drawClock();
    return;
  }
  if (contains(CLOCK_H_MINUS, x, y)) {
    adjustSelectedClock(-3600);
    drawClock();
    return;
  }
  if (contains(CLOCK_H_PLUS, x, y)) {
    adjustSelectedClock(3600);
    drawClock();
    return;
  }
  if (contains(CLOCK_M_MINUS, x, y)) {
    adjustSelectedClock(-60);
    drawClock();
    return;
  }
  if (contains(CLOCK_M_PLUS, x, y)) {
    adjustSelectedClock(60);
    drawClock();
  }
}

void setCronPreset(uint8_t value) {
  cronPreset = value % CRON_PRESET_COUNT;
  cronCustom = false;
}

void adjustCronParam(uint8_t row, int8_t direction) {
  if (cronPreset == 0 && row == 0) {
    cronIntervalMinutes = static_cast<uint8_t>(clampInt32(static_cast<int32_t>(cronIntervalMinutes) + direction * 5, 1, 59));
  } else if (cronPreset == 1 && row == 0) {
    cronMinute = static_cast<uint8_t>(clampInt32(static_cast<int32_t>(cronMinute) + direction * 5, 0, 59));
  } else if (cronPreset > 1 && cronPreset < 6) {
    if (row == 0) {
      cronHour = static_cast<uint8_t>((static_cast<int32_t>(cronHour) + direction + 24) % 24);
    } else if (row == 1) {
      cronMinute = static_cast<uint8_t>(clampInt32(static_cast<int32_t>(cronMinute) + direction * 5, 0, 59));
    }
  }
}

void startCronNumberEditor(uint8_t row) {
  if (cronPreset == 0 && row == 0) {
    startNumberEditor(NumberTarget::CronInterval, "Cron interval", cronIntervalMinutes, 1, 59, "min", 2);
  } else if (cronPreset == 1 && row == 0) {
    startNumberEditor(NumberTarget::CronMinute, "Cron minute", cronMinute, 0, 59, "", 2);
  } else if (cronPreset > 1 && cronPreset < 6 && row == 0) {
    startNumberEditor(NumberTarget::CronHour, "Cron hour", cronHour, 0, 23, "", 2);
  } else if (cronPreset > 1 && cronPreset < 6 && row == 1) {
    startNumberEditor(NumberTarget::CronMinute, "Cron minute", cronMinute, 0, 59, "", 2);
  } else {
    return;
  }
  drawNumberEditor();
}

void handleCronTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    goHome();
    return;
  }
  for (uint8_t field = 0; field < 5; ++field) {
    if (contains(cronFieldRect(field, 42), x, y)) {
      startCronEditor(field);
      drawCronEdit();
      return;
    }
  }
  if (contains(CRON_PRESET_BUTTON, x, y)) {
    openChoiceDialog(ChoiceTarget::CronPreset, "SELECT CRON PRESET");
    drawChoiceDialog();
    return;
  }
  for (uint8_t row = 0; row < 2; ++row) {
    if (contains(cronMinusRect(row), x, y)) {
      if (cronCustom) {
        return;
      }
      adjustCronParam(row, -1);
      drawCron();
      return;
    }
    if (contains(cronPlusRect(row), x, y)) {
      if (cronCustom) {
        return;
      }
      adjustCronParam(row, 1);
      drawCron();
      return;
    }
    if (contains(cronCenterRect(row), x, y)) {
      if (cronCustom) {
        startCronEditor(row);
        drawCronEdit();
        return;
      }
      startCronNumberEditor(row);
      return;
    }
  }
}

void handleCronEditTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y) || contains(cronEditCancelRect(), x, y)) {
    currentScreen = Screen::Cron;
    drawCron();
    return;
  }

  for (uint8_t field = 0; field < 5; ++field) {
    if (contains(cronFieldRect(field, 42), x, y)) {
      cronEditSelectedField = field;
      cronEditInvalidField = -1;
      cronEditError = "";
      drawCronEdit();
      return;
    }
  }

  const char* keys[15] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "*", "/", "-", ",", "DEL"};
  for (uint8_t i = 0; i < 15; ++i) {
    if (!contains(cronEditKeyRect(i), x, y)) {
      continue;
    }
    String& field = cronEditFields[cronEditSelectedField];
    if (i == 14) {
      if (field.length() > 0) {
        field.remove(field.length() - 1);
      }
    } else if (field.length() < 12) {
      field += keys[i];
    }
    cronEditInvalidField = -1;
    cronEditError = "";
    drawCronEdit();
    return;
  }

  if (contains(cronEditClearRect(), x, y)) {
    cronEditFields[cronEditSelectedField] = "";
    cronEditInvalidField = -1;
    cronEditError = "";
    drawCronEdit();
    return;
  }
  if (contains(cronEditApplyRect(), x, y)) {
    CronSchedule schedule{};
    uint8_t invalidField = 0;
    String error;
    if (!validateCronFields(cronEditFields, schedule, invalidField, error)) {
      cronEditSelectedField = invalidField;
      cronEditInvalidField = static_cast<int8_t>(invalidField);
      cronEditError = "Fix expression - " + error;
      drawCronEdit();
      return;
    }
    for (uint8_t i = 0; i < 5; ++i) {
      cronCustomFields[i] = cronEditFields[i];
    }
    cronEditInvalidField = -1;
    cronEditError = "";
    cronCustom = true;
    currentScreen = Screen::Cron;
    drawCron();
  }
}

void closeChoiceDialog() {
  choiceDialog.active = false;
  choiceDialog.target = ChoiceTarget::None;
}

void handleChoiceDialogTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    closeChoiceDialog();
    drawCurrentScreen();
    return;
  }

  const ChoiceTarget target = choiceDialog.target;
  const uint8_t count = choiceCount();
  for (uint8_t i = 0; i < count; ++i) {
    if (!contains(choiceRect(i), x, y)) {
      continue;
    }

    if (target == ChoiceTarget::CronPreset) {
      setCronPreset(i);
      closeChoiceDialog();
      currentScreen = Screen::Cron;
      drawCron();
      return;
    }
    if (target == ChoiceTarget::BackupFrequency) {
      backupFrequencyIndex = i;
      closeChoiceDialog();
      currentScreen = Screen::Capacity;
      storageTab = StorageTab::Backup;
      drawCapacity();
      return;
    }
    if (target == ChoiceTarget::BackupScheme) {
      backupSchemeIndex = i;
      closeChoiceDialog();
      storageTab = StorageTab::Backup;
      if (i == BACKUP_SCHEME_COUNT - 1) {
        currentScreen = Screen::BackupRetention;
        drawBackupRetention();
      } else {
        currentScreen = Screen::Capacity;
        drawCapacity();
      }
      return;
    }
  }
}

void adjustStorageValue(uint8_t row, int8_t direction) {
  if (storageTab == StorageTab::Storage) {
    switch (row) {
      case 0:
        storageDailyGb = static_cast<uint16_t>(clampInt32(static_cast<int32_t>(storageDailyGb) + direction * 5, 1, 50000));
        break;
      case 1:
        storageRetentionDays = static_cast<uint16_t>(clampInt32(static_cast<int32_t>(storageRetentionDays) + direction * 7, 1, 3650));
        break;
      case 2:
        storageCompressionPct = static_cast<uint8_t>(clampInt32(static_cast<int32_t>(storageCompressionPct) + direction * 5, 5, 100));
        break;
      case 3:
        storageCopies = static_cast<uint8_t>(clampInt32(static_cast<int32_t>(storageCopies) + direction, 1, 20));
        break;
      default:
        storageSparePct = static_cast<uint8_t>(clampInt32(static_cast<int32_t>(storageSparePct) + direction * 5, 0, 100));
        break;
    }
  } else {
    switch (row) {
      case 0:
        backupFullGb = static_cast<uint16_t>(clampInt32(static_cast<int32_t>(backupFullGb) + direction * 25, 1, 50000));
        break;
      case 1:
        backupIncrementalGb = static_cast<uint16_t>(clampInt32(static_cast<int32_t>(backupIncrementalGb) + direction * 5, 1, 50000));
        break;
      case 2:
      case 3:
        // Frequency and retention scheme use full-screen selectors.
        break;
      default:
        backupCopies = static_cast<uint8_t>(clampInt32(static_cast<int32_t>(backupCopies) + direction, 1, 20));
        break;
    }
  }
}

void startCapacityNumberEditor(uint8_t row) {
  if (storageTab == StorageTab::Storage) {
    switch (row) {
      case 0:
        startNumberEditor(NumberTarget::StorageDaily, "Daily data", storageDailyGb, 1, 50000, "GB/day", 5);
        break;
      case 1:
        startNumberEditor(NumberTarget::StorageRetention, "Retention", storageRetentionDays, 1, 3650, "days", 4);
        break;
      case 2:
        startNumberEditor(NumberTarget::StorageCompression, "Compression", storageCompressionPct, 5, 100, "%", 3);
        break;
      case 3:
        startNumberEditor(NumberTarget::StorageCopies, "Copies", storageCopies, 1, 20, "", 2);
        break;
      default:
        startNumberEditor(NumberTarget::StorageSpare, "Free space", storageSparePct, 0, 100, "%", 3);
        break;
    }
    drawNumberEditor();
  } else {
    switch (row) {
      case 0:
        startNumberEditor(NumberTarget::BackupFull, "Full backup", backupFullGb, 1, 50000, "GB", 5);
        drawNumberEditor();
        break;
      case 1:
        startNumberEditor(NumberTarget::BackupIncremental, "Incremental", backupIncrementalGb, 1, 50000, "GB", 5);
        drawNumberEditor();
        break;
      case 2:
        openChoiceDialog(ChoiceTarget::BackupFrequency, "BACKUP FREQUENCY");
        drawChoiceDialog();
        break;
      case 3:
        openChoiceDialog(ChoiceTarget::BackupScheme, "RETENTION SCHEME");
        drawChoiceDialog();
        break;
      case 4:
        startNumberEditor(NumberTarget::BackupCopies, "Backup copies", backupCopies, 1, 20, "", 2);
        drawNumberEditor();
        break;
      default:
        break;
    }
  }
}

void handleCapacityTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    goHome();
    return;
  }
  if (contains(STORAGE_TAB_STORAGE, x, y)) {
    storageTab = StorageTab::Storage;
    drawCapacity();
    return;
  }
  if (contains(STORAGE_TAB_BACKUP, x, y)) {
    storageTab = StorageTab::Backup;
    drawCapacity();
    return;
  }
  for (uint8_t row = 0; row < 5; ++row) {
    if (storageTab == StorageTab::Backup && (row == 2 || row == 3) &&
        (contains(capacityChoiceRect(row), x, y) || contains(capacityPlusRect(row), x, y))) {
      startCapacityNumberEditor(row);
      return;
    }
    if (contains(capacityMinusRect(row), x, y)) {
      adjustStorageValue(row, -1);
      drawCapacity();
      return;
    }
    if (contains(capacityPlusRect(row), x, y)) {
      adjustStorageValue(row, 1);
      drawCapacity();
      return;
    }
    if (contains(capacityCenterRect(row), x, y)) {
      startCapacityNumberEditor(row);
      return;
    }
  }
}

void adjustBackupRetention(uint8_t row, int8_t direction) {
  uint8_t* value = nullptr;
  uint8_t maximum = 99;
  if (row == 0) {
    value = &backupCustomDaily;
    maximum = 90;
  } else if (row == 1) {
    value = &backupCustomWeekly;
    maximum = 52;
  } else {
    value = &backupCustomMonthly;
    maximum = 60;
  }
  *value = static_cast<uint8_t>(clampInt32(static_cast<int32_t>(*value) + direction, 0, maximum));
}

void startBackupRetentionEditor(uint8_t row) {
  if (row == 0) {
    startNumberEditor(NumberTarget::BackupDaily, "Daily restore points", backupCustomDaily, 0, 90, "", 2);
  } else if (row == 1) {
    startNumberEditor(NumberTarget::BackupWeekly, "Weekly restore points", backupCustomWeekly, 0, 52, "", 2);
  } else {
    startNumberEditor(NumberTarget::BackupMonthly, "Monthly restore points", backupCustomMonthly, 0, 60, "", 2);
  }
  drawNumberEditor();
}

void returnToBackupCapacity() {
  backupSchemeIndex = BACKUP_SCHEME_COUNT - 1;
  storageTab = StorageTab::Backup;
  currentScreen = Screen::Capacity;
  drawCapacity();
}

void handleBackupRetentionTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y) || contains(RETENTION_APPLY, x, y)) {
    returnToBackupCapacity();
    return;
  }
  for (uint8_t row = 0; row < 3; ++row) {
    if (contains(retentionMinusRect(row), x, y)) {
      adjustBackupRetention(row, -1);
      drawBackupRetention();
      return;
    }
    if (contains(retentionCenterRect(row), x, y)) {
      startBackupRetentionEditor(row);
      return;
    }
    if (contains(retentionPlusRect(row), x, y)) {
      adjustBackupRetention(row, 1);
      drawBackupRetention();
      return;
    }
  }
}

void handleChmodTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    goHome();
    return;
  }
  Rect resultPanel{12, 41, 296, 38};
  if (contains(resultPanel, x, y)) {
    const uint16_t current = chmodValueForColumn(0) * 100 + chmodValueForColumn(1) * 10 + chmodValueForColumn(2);
    startNumberEditor(NumberTarget::ChmodMode, "chmod mode", current, 0, 777, "", 3, true);
    drawNumberEditor();
    return;
  }

  for (uint8_t row = 0; row < 3; ++row) {
    for (uint8_t col = 0; col < 3; ++col) {
      Rect cell{static_cast<int16_t>(12 + col * 100), static_cast<int16_t>(98 + row * 32), 92, 27};
      if (contains(cell, x, y)) {
        chmodBits[col][row] = !chmodBits[col][row];
        drawChmod();
        return;
      }
    }
  }

  const uint16_t presets[] = {600, 644, 755, 777};
  for (uint8_t i = 0; i < 4; ++i) {
    Rect preset{static_cast<int16_t>(12 + i * 75), 197, 70, 25};
    if (contains(preset, x, y)) {
      applyChmodPreset(presets[i]);
      drawChmod();
      return;
    }
  }
}

void handleReferenceTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    goHome();
    return;
  }
  if (contains(REF_HTTP_TAB, x, y)) {
    referenceShowsHttp = true;
    referencePage = 0;
    drawReference();
    return;
  }
  if (contains(REF_PORT_TAB, x, y)) {
    referenceShowsHttp = false;
    referencePage = 0;
    drawReference();
    return;
  }
  if (contains(REF_PREV, x, y) && referencePage > 0) {
    --referencePage;
    drawReference();
    return;
  }
  if (contains(REF_NEXT, x, y) && referencePage + 1 < referencePageCount()) {
    ++referencePage;
    drawReference();
  }
}

void handleWirelessTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    goHome();
    return;
  }
  if (contains(WIRELESS_WIFI_TOGGLE, x, y)) {
    if (wirelessTab == WirelessTab::Wifi && wifiEnabled) {
      disableWifi();
    } else {
      enableWifi();
    }
    drawWireless();
    return;
  }
  if (contains(WIRELESS_BLE_TOGGLE, x, y)) {
    if (wirelessTab == WirelessTab::Ble && bleEnabled) {
      disableBle();
    } else {
      enableBle();
    }
    drawWireless();
    return;
  }
  if (contains(WIRELESS_SCAN, x, y)) {
    if (wirelessTab == WirelessTab::Wifi && wifiEnabled) {
      scanWifiNetworks();
    } else if (wirelessTab == WirelessTab::Ble && bleEnabled) {
      scanBleDevices();
    }
    drawWireless();
    return;
  }
  if (contains(WIRELESS_MODE, x, y)) {
    const bool radioOn = wirelessTab == WirelessTab::Wifi ? wifiEnabled : bleEnabled;
    if (!radioOn) {
      return;
    }
    if (wirelessTab == WirelessTab::Wifi) {
      wifiPassiveScan = !wifiPassiveScan;
      wirelessStatus = wifiPassiveScan ? "Passive WiFi scan selected" : "Active WiFi scan selected";
    } else {
      bleActiveScan = !bleActiveScan;
      if (bleScanner != nullptr) {
        bleScanner->setActiveScan(bleActiveScan);
      }
      wirelessStatus = bleActiveScan ? "Active BLE scan selected" : "Passive BLE scan selected";
    }
    drawWireless();
    return;
  }
  if (contains(WIRELESS_VIEW, x, y)) {
    if (wirelessTab == WirelessTab::Wifi && wifiEnabled) {
      wifiChannelView = !wifiChannelView;
      drawWireless();
    } else if (wirelessTab == WirelessTab::Ble && bleEnabled && bleCount > 0) {
      bleScanner->clearResults();
      bleCount = 0;
      bleTotalFound = 0;
      blePage = 0;
      wirelessStatus = "BLE results cleared";
      drawWireless();
    }
    return;
  }
  if (wirelessTab == WirelessTab::Wifi) {
    if (!wifiChannelView && wifiEnabled) {
      for (uint8_t visibleRow = 0; visibleRow < ITEMS_PER_WIRELESS_PAGE; ++visibleRow) {
        if (!contains(wifiListRowRect(visibleRow), x, y)) {
          continue;
        }
        const uint8_t index = wifiPage * ITEMS_PER_WIRELESS_PAGE + visibleRow;
        if (index < wifiCount) {
          startWifiMonitor(index);
        }
        return;
      }
    }
    const uint8_t wifiPages = (wifiCount + ITEMS_PER_WIRELESS_PAGE - 1) / ITEMS_PER_WIRELESS_PAGE;
    if (contains(WIRELESS_PREV, x, y) && !wifiChannelView && wifiPage > 0) {
      --wifiPage;
      drawWireless();
      return;
    }
    if (contains(WIRELESS_NEXT, x, y) && !wifiChannelView && wifiPage + 1 < wifiPages) {
      ++wifiPage;
      drawWireless();
      return;
    }
  } else {
    const uint8_t blePages = (bleCount + ITEMS_PER_WIRELESS_PAGE - 1) / ITEMS_PER_WIRELESS_PAGE;
    if (contains(WIRELESS_PREV, x, y) && blePage > 0) {
      --blePage;
      drawWireless();
      return;
    }
    if (contains(WIRELESS_NEXT, x, y) && blePage + 1 < blePages) {
      ++blePage;
      drawWireless();
    }
  }
}

void handleWifiMonitorTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y) || contains(WIFI_MONITOR_LIST, x, y)) {
    currentScreen = Screen::Wireless;
    wirelessTab = WirelessTab::Wifi;
    drawWireless();
    return;
  }
  if (contains(WIFI_MONITOR_RESCAN, x, y)) {
    scanMonitoredWifi();
    drawWifiMonitorDynamic();
  }
}

void handleTouch(uint16_t x, uint16_t y) {
  if (numberEditor.active) {
    handleNumberEditorTouch(x, y);
    return;
  }
  if (choiceDialog.active) {
    handleChoiceDialogTouch(x, y);
    return;
  }

  switch (currentScreen) {
    case Screen::Home:
      handleHomeTouch(x, y);
      break;
    case Screen::Cidr:
      handleCidrTouch(x, y);
      break;
    case Screen::Vlsm:
      handleVlsmTouch(x, y);
      break;
    case Screen::Clock:
      handleClockTouch(x, y);
      break;
    case Screen::Cron:
      handleCronTouch(x, y);
      break;
    case Screen::CronEdit:
      handleCronEditTouch(x, y);
      break;
    case Screen::Capacity:
      handleCapacityTouch(x, y);
      break;
    case Screen::BackupRetention:
      handleBackupRetentionTouch(x, y);
      break;
    case Screen::Chmod:
      handleChmodTouch(x, y);
      break;
    case Screen::Wireless:
      handleWirelessTouch(x, y);
      break;
    case Screen::WifiMonitor:
      handleWifiMonitorTouch(x, y);
      break;
    case Screen::Reference:
      handleReferenceTouch(x, y);
      break;
  }
}

void runWifiMonitorIfNeeded() {
  if (currentScreen != Screen::WifiMonitor || numberEditor.active || choiceDialog.active || !wifiMonitorHasTarget) {
    return;
  }
  if (millis() - lastWifiMonitorAt < 1800) {
    return;
  }
  scanMonitoredWifi();
  drawWifiMonitorDynamic();
}

void showSplash() {
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setTextFont(2);
  tft.drawString("FROM BITCOIN MINER TO", SCREEN_W / 2, 68);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextFont(4);
  tft.drawString("ADMIN TOOLKIT", SCREEN_W / 2, 105);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setTextFont(1);
  tft.drawString("Offline admin utilities. Touch friendly.", SCREEN_W / 2, 139);
  tft.drawRoundRect(60, 166, 200, 5, 2, COLOR_BORDER);
  for (int16_t width = 0; width <= 196; width += 14) {
    tft.fillRoundRect(62, 168, width, 1, 1, COLOR_ACCENT);
    delay(45);
  }
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setTextFont(1);
  tft.drawString("Created by Wojciech Lepczynski", SCREEN_W / 2, 202);
  delay(350);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  WiFi.mode(WIFI_OFF);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(false);

  if (RUN_TOUCH_CALIBRATION) {
    uint16_t calibrated[5] = {0, 0, 0, 0, 0};
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.setCursor(16, 12);
    tft.println("Touch the corners as shown");
    tft.calibrateTouch(calibrated, TFT_MAGENTA, TFT_BLACK, 15);

    Serial.println("Copy these values into touchCalibration:");
    Serial.printf("{%u, %u, %u, %u, %u}\n", calibrated[0], calibrated[1], calibrated[2],
                  calibrated[3], calibrated[4]);
    for (uint8_t i = 0; i < 5; ++i) {
      touchCalibration[i] = calibrated[i];
    }
    delay(1500);
  }

  tft.setTouch(touchCalibration);

  initializeClockCalendarFromBuildDate();
  lastClockMillis = millis();
  showSplash();
  drawCurrentScreen();
}

void loop() {
  uint16_t touchX = 0;
  uint16_t touchY = 0;
  const bool touched = tft.getTouch(&touchX, &touchY, 600);

  if (touched && !wasTouched && millis() - lastTouchAt > 120) {
    Serial.printf("Touch x=%u y=%u\n", touchX, touchY);
    handleTouch(touchX, touchY);
    lastTouchAt = millis();
  }

  if (currentScreen == Screen::Clock && !numberEditor.active) {
    syncClock();
    updateClockRows(false);
  }

  runWifiMonitorIfNeeded();

  wasTouched = touched;
  delay(12);
}
