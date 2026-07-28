#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

#include <cstdint>
#include <cstdio>

namespace {

TFT_eSPI tft;

// Elecrow's factory firmware for the 2.8-inch Miner uses TFT_eSPI's built-in
// XPT2046 support. The touch controller shares SCLK/MOSI with the display,
// uses GPIO 4 for MISO and GPIO 33 for chip select. Those pins are configured
// in platformio.ini.
constexpr int16_t SCREEN_W = 320;
constexpr int16_t SCREEN_H = 240;
constexpr uint8_t BACKLIGHT_PIN = 27;
constexpr bool RUN_TOUCH_CALIBRATION = false;

// Default calibration published in Elecrow's factory source for this model.
// If touch is offset on your unit, set RUN_TOUCH_CALIBRATION to true once,
// upload, follow the corner prompts and copy the values printed to Serial.
uint16_t touchCalibration[5] = {189, 3416, 359, 3439, 1};

constexpr uint16_t COLOR_BG = 0x0861;
constexpr uint16_t COLOR_PANEL = 0x10C3;
constexpr uint16_t COLOR_PANEL_2 = 0x1925;
constexpr uint16_t COLOR_ACCENT = 0x2E9F;
constexpr uint16_t COLOR_ACCENT_2 = 0x5D5F;
constexpr uint16_t COLOR_TEXT = TFT_WHITE;
constexpr uint16_t COLOR_MUTED = 0xBDF7;
constexpr uint16_t COLOR_SUCCESS = 0x4E69;
constexpr uint16_t COLOR_WARNING = 0xFD20;
constexpr uint16_t COLOR_DANGER = 0xF249;
constexpr uint16_t COLOR_BORDER = 0x31A6;

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

enum class Screen {
  Home,
  Cidr,
  Chmod,
  Reference,
};

struct ReferenceItem {
  const char* key;
  const char* description;
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
    {"2375", "Docker API (plain)"},
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

constexpr size_t HTTP_COUNT = sizeof(HTTP_ITEMS) / sizeof(HTTP_ITEMS[0]);
constexpr size_t PORT_COUNT = sizeof(PORT_ITEMS) / sizeof(PORT_ITEMS[0]);
constexpr size_t ITEMS_PER_PAGE = 5;

Screen currentScreen = Screen::Home;
uint8_t cidrPrefix = 24;
bool chmodBits[3][3] = {
    {true, true, true},
    {true, false, true},
    {true, false, true},
};
bool referenceShowsHttp = true;
size_t referencePage = 0;
bool wasTouched = false;
uint32_t lastTouchAt = 0;

const Rect HOME_CIDR{12, 48, 144, 70};
const Rect HOME_CHMOD{164, 48, 144, 70};
const Rect HOME_REFERENCE{12, 128, 296, 70};
const Rect BACK_BUTTON{6, 5, 54, 27};
const Rect CIDR_MINUS{12, 48, 62, 48};
const Rect CIDR_PLUS{246, 48, 62, 48};
const Rect REF_HTTP_TAB{12, 39, 143, 30};
const Rect REF_PORT_TAB{165, 39, 143, 30};
const Rect REF_PREV{12, 207, 88, 28};
const Rect REF_NEXT{220, 207, 88, 28};

bool contains(const Rect& rect, uint16_t x, uint16_t y) {
  return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

void drawRoundedPanel(const Rect& rect, uint16_t fill, uint16_t border = COLOR_BORDER, uint8_t radius = 8) {
  tft.fillRoundRect(rect.x, rect.y, rect.w, rect.h, radius, fill);
  tft.drawRoundRect(rect.x, rect.y, rect.w, rect.h, radius, border);
}

void drawButton(const Rect& rect, const char* label, uint16_t fill, uint16_t textColor = COLOR_TEXT,
                uint8_t font = 2) {
  drawRoundedPanel(rect, fill);
  tft.setTextColor(textColor, fill);
  tft.setTextFont(font);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, rect.x + rect.w / 2, rect.y + rect.h / 2 + 1);
}

void drawHeader(const char* title, bool withBack = true) {
  tft.fillRect(0, 0, SCREEN_W, 36, COLOR_PANEL);
  tft.drawFastHLine(0, 35, SCREEN_W, COLOR_BORDER);
  if (withBack) {
    drawButton(BACK_BUTTON, "< HOME", COLOR_PANEL_2, COLOR_TEXT, 1);
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

void drawHome() {
  tft.fillScreen(COLOR_BG);
  drawHeader("ADMIN POCKET TOOLKIT", false);

  drawRoundedPanel(HOME_CIDR, COLOR_PANEL);
  tft.setTextColor(COLOR_ACCENT_2, COLOR_PANEL);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CIDR", HOME_CIDR.x + HOME_CIDR.w / 2, HOME_CIDR.y + 25);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.setTextFont(1);
  tft.drawString("mask & host calculator", HOME_CIDR.x + HOME_CIDR.w / 2, HOME_CIDR.y + 51);

  drawRoundedPanel(HOME_CHMOD, COLOR_PANEL);
  tft.setTextColor(COLOR_SUCCESS, COLOR_PANEL);
  tft.setTextFont(4);
  tft.drawString("CHMOD", HOME_CHMOD.x + HOME_CHMOD.w / 2, HOME_CHMOD.y + 25);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.setTextFont(1);
  tft.drawString("visual permissions", HOME_CHMOD.x + HOME_CHMOD.w / 2, HOME_CHMOD.y + 51);

  drawRoundedPanel(HOME_REFERENCE, COLOR_PANEL);
  tft.setTextColor(COLOR_WARNING, COLOR_PANEL);
  tft.setTextFont(4);
  tft.drawString("HTTP & PORTS", HOME_REFERENCE.x + HOME_REFERENCE.w / 2, HOME_REFERENCE.y + 24);
  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.setTextFont(1);
  tft.drawString("offline quick reference", HOME_REFERENCE.x + HOME_REFERENCE.w / 2,
                 HOME_REFERENCE.y + 51);

  drawFooter("LOCAL TOOLKIT  |  TOUCH ENABLED  |  NO NETWORK REQUIRED");
}

String ipv4FromUint(uint32_t value) {
  char buffer[20];
  std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", static_cast<unsigned>((value >> 24) & 0xFF),
                static_cast<unsigned>((value >> 16) & 0xFF),
                static_cast<unsigned>((value >> 8) & 0xFF), static_cast<unsigned>(value & 0xFF));
  return String(buffer);
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

void drawCidr() {
  tft.fillScreen(COLOR_BG);
  drawHeader("CIDR CALCULATOR");

  drawButton(CIDR_MINUS, "-", COLOR_PANEL_2, COLOR_TEXT, 4);
  drawButton(CIDR_PLUS, "+", COLOR_PANEL_2, COLOR_TEXT, 4);

  Rect prefixPanel{82, 48, 156, 48};
  drawRoundedPanel(prefixPanel, COLOR_PANEL);
  tft.setTextColor(COLOR_ACCENT_2, COLOR_PANEL);
  tft.setTextFont(4);
  tft.setTextDatum(MC_DATUM);
  char prefixText[8];
  std::snprintf(prefixText, sizeof(prefixText), "/%u", cidrPrefix);
  tft.drawString(prefixText, prefixPanel.x + prefixPanel.w / 2, prefixPanel.y + prefixPanel.h / 2);

  const uint8_t quickPrefixes[] = {8, 16, 24, 32};
  for (uint8_t i = 0; i < 4; ++i) {
    Rect quick{static_cast<int16_t>(12 + i * 76), 104, 68, 28};
    char quickLabel[6];
    std::snprintf(quickLabel, sizeof(quickLabel), "/%u", quickPrefixes[i]);
    drawButton(quick, quickLabel, cidrPrefix == quickPrefixes[i] ? COLOR_ACCENT : COLOR_PANEL_2,
               COLOR_TEXT, 2);
  }

  const uint32_t mask = cidrPrefix == 0 ? 0U : (0xFFFFFFFFU << (32U - cidrPrefix));
  const uint32_t wildcard = ~mask;
  const uint64_t addresses = 1ULL << (32U - cidrPrefix);
  uint64_t usable = addresses;
  if (cidrPrefix <= 30) {
    usable = addresses - 2;
  }

  Rect details{12, 140, 296, 75};
  drawRoundedPanel(details, COLOR_PANEL);
  tft.setTextFont(2);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(COLOR_MUTED, COLOR_PANEL);
  tft.drawString("Subnet mask", 22, 147);
  tft.drawString("Wildcard", 22, 166);
  tft.drawString("Addresses", 22, 185);
  tft.drawString("Usable hosts", 22, 204);

  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(ipv4FromUint(mask), 298, 147);
  tft.drawString(ipv4FromUint(wildcard), 298, 166);
  tft.drawString(formatNumber(addresses), 298, 185);
  tft.drawString(formatNumber(usable), 298, 204);

  drawFooter("Tap +/- or choose a common prefix");
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
  const uint8_t values[3] = {static_cast<uint8_t>((mode / 100) % 10),
                             static_cast<uint8_t>((mode / 10) % 10),
                             static_cast<uint8_t>(mode % 10)};
  for (uint8_t col = 0; col < 3; ++col) {
    chmodBits[col][0] = (values[col] & 4U) != 0;
    chmodBits[col][1] = (values[col] & 2U) != 0;
    chmodBits[col][2] = (values[col] & 1U) != 0;
  }
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
  tft.setTextFont(2);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(chmodSymbolic(), 299, 60);

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
      drawButton(cell, rowLabels[row], active ? COLOR_ACCENT : COLOR_PANEL_2,
                 active ? COLOR_TEXT : COLOR_MUTED, 2);
    }
  }

  const uint16_t presets[] = {644, 755, 700};
  for (uint8_t i = 0; i < 3; ++i) {
    Rect preset{static_cast<int16_t>(12 + i * 100), 197, 92, 25};
    char label[8];
    std::snprintf(label, sizeof(label), "%u", presets[i]);
    drawButton(preset, label, COLOR_PANEL_2, COLOR_TEXT, 2);
  }

  drawFooter("Toggle R/W/X or choose a preset");
}

size_t referencePageCount() {
  const size_t count = referenceShowsHttp ? HTTP_COUNT : PORT_COUNT;
  return (count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
}

void drawReference() {
  tft.fillScreen(COLOR_BG);
  drawHeader("QUICK REFERENCE");

  drawButton(REF_HTTP_TAB, "HTTP STATUS", referenceShowsHttp ? COLOR_ACCENT : COLOR_PANEL_2,
             COLOR_TEXT, 2);
  drawButton(REF_PORT_TAB, "COMMON PORTS", referenceShowsHttp ? COLOR_PANEL_2 : COLOR_ACCENT,
             COLOR_TEXT, 2);

  const ReferenceItem* items = referenceShowsHttp ? HTTP_ITEMS : PORT_ITEMS;
  const size_t count = referenceShowsHttp ? HTTP_COUNT : PORT_COUNT;
  const size_t start = referencePage * ITEMS_PER_PAGE;

  for (size_t i = 0; i < ITEMS_PER_PAGE; ++i) {
    const size_t index = start + i;
    const int16_t y = 75 + static_cast<int16_t>(i * 25);
    Rect row{12, y, 296, 22};
    tft.fillRoundRect(row.x, row.y, row.w, row.h, 4, i % 2 == 0 ? COLOR_PANEL : COLOR_PANEL_2);
    if (index >= count) {
      continue;
    }

    const uint16_t rowColor = i % 2 == 0 ? COLOR_PANEL : COLOR_PANEL_2;
    tft.setTextColor(referenceShowsHttp ? COLOR_WARNING : COLOR_ACCENT_2, rowColor);
    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(items[index].key, 20, y + 11);

    tft.setTextColor(COLOR_TEXT, rowColor);
    tft.setTextFont(1);
    tft.drawString(items[index].description, 78, y + 11);
  }

  drawButton(REF_PREV, "< PREV", referencePage > 0 ? COLOR_PANEL_2 : COLOR_BG,
             referencePage > 0 ? COLOR_TEXT : COLOR_MUTED, 1);
  drawButton(REF_NEXT, "NEXT >", referencePage + 1 < referencePageCount() ? COLOR_PANEL_2 : COLOR_BG,
             referencePage + 1 < referencePageCount() ? COLOR_TEXT : COLOR_MUTED, 1);

  char pageText[20];
  std::snprintf(pageText, sizeof(pageText), "%u / %u", static_cast<unsigned>(referencePage + 1),
                static_cast<unsigned>(referencePageCount()));
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(pageText, SCREEN_W / 2, 221);
}

void drawCurrentScreen() {
  switch (currentScreen) {
    case Screen::Home:
      drawHome();
      break;
    case Screen::Cidr:
      drawCidr();
      break;
    case Screen::Chmod:
      drawChmod();
      break;
    case Screen::Reference:
      drawReference();
      break;
  }
}

void goHome() {
  currentScreen = Screen::Home;
  drawHome();
}

void handleHomeTouch(uint16_t x, uint16_t y) {
  if (contains(HOME_CIDR, x, y)) {
    currentScreen = Screen::Cidr;
    drawCidr();
  } else if (contains(HOME_CHMOD, x, y)) {
    currentScreen = Screen::Chmod;
    drawChmod();
  } else if (contains(HOME_REFERENCE, x, y)) {
    currentScreen = Screen::Reference;
    referencePage = 0;
    drawReference();
  }
}

void handleCidrTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    goHome();
    return;
  }
  if (contains(CIDR_MINUS, x, y) && cidrPrefix > 0) {
    --cidrPrefix;
    drawCidr();
    return;
  }
  if (contains(CIDR_PLUS, x, y) && cidrPrefix < 32) {
    ++cidrPrefix;
    drawCidr();
    return;
  }
  const uint8_t quickPrefixes[] = {8, 16, 24, 32};
  for (uint8_t i = 0; i < 4; ++i) {
    Rect quick{static_cast<int16_t>(12 + i * 76), 104, 68, 28};
    if (contains(quick, x, y)) {
      cidrPrefix = quickPrefixes[i];
      drawCidr();
      return;
    }
  }
}

void handleChmodTouch(uint16_t x, uint16_t y) {
  if (contains(BACK_BUTTON, x, y)) {
    goHome();
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

  const uint16_t presets[] = {644, 755, 700};
  for (uint8_t i = 0; i < 3; ++i) {
    Rect preset{static_cast<int16_t>(12 + i * 100), 197, 92, 25};
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

void handleTouch(uint16_t x, uint16_t y) {
  switch (currentScreen) {
    case Screen::Home:
      handleHomeTouch(x, y);
      break;
    case Screen::Cidr:
      handleCidrTouch(x, y);
      break;
    case Screen::Chmod:
      handleChmodTouch(x, y);
      break;
    case Screen::Reference:
      handleReferenceTouch(x, y);
      break;
  }
}

void showSplash() {
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setTextFont(2);
  tft.drawString("FROM BITCOIN MINER TO", SCREEN_W / 2, 68);
  tft.setTextColor(COLOR_ACCENT_2, COLOR_BG);
  tft.setTextFont(4);
  tft.drawString("ADMIN TOOLKIT", SCREEN_W / 2, 105);
  tft.setTextColor(COLOR_MUTED, COLOR_BG);
  tft.setTextFont(1);
  tft.drawString("Offline. Useful. No cloud required.", SCREEN_W / 2, 139);
  tft.drawRoundRect(60, 166, 200, 5, 2, COLOR_BORDER);
  for (int16_t width = 0; width <= 196; width += 14) {
    tft.fillRoundRect(62, 168, width, 1, 1, COLOR_ACCENT);
    delay(45);
  }
  delay(350);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

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

  wasTouched = touched;
  delay(12);
}
