#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ChronosESP32.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <FluxGarage_RoboEyes.h>
#include <time.h>

#undef N
#undef E

// ============================================================
// STRUCT DEFINITIONS
// ============================================================
struct MelodyNote {
  uint16_t frequency;
  uint16_t duration;
};

static constexpr int ZORA_EYE_POSITION_EAST = 3;
static constexpr int ZORA_EYE_POSITION_WEST = 7;

// ============================================================
// ZEVRON - ESP32-C3 SUPER MINI BY ZDYVOID (v1.2.39 - BLE BOOT OFF FIXED)
// ============================================================

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C
#define OLED_SDA 20
#define OLED_SCL 21
#define TOUCH_PIN 5
#define BUZZER_PIN 6
#define TOUCH_ACTIVE_LEVEL HIGH

// ============================================================
// OTA CONFIGURATION
// ============================================================
#define ZEVRON_FIRMWARE_VERSION "1.2.43"
const char* GITHUB_RELEASE_API = "https://api.github.com/repos/zaldyprtm/zevron-desk-companion/releases/latest";
const char* GITHUB_USER_AGENT = "ZEVRON-Desk-Companion";
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000UL;

// ============================================================
// LOCATION & TIME CONFIGURATION (Pangkalpinang - UTC+7)
// ============================================================
const float LATITUDE  = -2.1289;
const float LONGITUDE = 106.1130;
const long  GMT_OFFSET_SEC = 7 * 3600; 
const int   DAYLIGHT_OFFSET_SEC = 0;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
RoboEyes<Adafruit_SSD1306> roboEyes(display);
Preferences preferences;
ChronosESP32 chronosWatch("ZEVRON-ZDY");
WiFiManager wm;

enum Personality {
  PERS_CHILL,
  PERS_ENERGETIC,
  PERS_TSUNDERE,
  PERS_ROBOT
};
Personality zevronPersonality = PERS_CHILL;

enum AppMode {
  MODE_HOME,
  MODE_SETTINGS,
  MODE_SOUND_STATUS,         // Layar Status Sound FX
  MODE_BLE_STATUS,           // Layar Status Chronos BLE
  MODE_PERSONALITY_STATUS,   // Layar Status Personality
  MODE_BRIGHTNESS_STATUS,    // Layar Status OLED Brightness
  MODE_LIGHT_STATUS,         // Layar Status Light Mode
  MODE_CLOCK,
  MODE_WEATHER_ONDEMAND,
  MODE_ALIEN_LOG,
  MODE_SYS_MONITOR,
  MODE_DEEP_SLEEP,
  MODE_NAVIGATION,
  MODE_DREAM,
  MODE_GAMES_MENU,
  MODE_DINO_GAME,
  MODE_FISH_GAME,
  MODE_OTA,
  MODE_SLEEP,
  MODE_ALIEN_SIGNAL,
  MODE_RADAR_SAVER,
  MODE_ZEVRON_INFO
};

AppMode currentMode = MODE_HOME;

bool soundOn = true;
bool bleEnabled = false;
bool bleConnected = false;
bool lightModeEnabled = false;
bool isDimmed = false;
int oledBrightness = 255;

// CHRONOS BLE: manual start, independent Google Maps navigation.
volatile bool chronosConnectionEvent = false;
bool chronosCallbacksReady = false;

// Lightweight navigation state; do not keep a second 48x48 icon buffer.
bool navActive = false;
String navTitle = "";
String navDirections = "";
String navEta = "";
String navDistance = "";
uint32_t navIconCRC = 0;
bool navHasIcon = false;
unsigned long lastNavPoll = 0;

void resetDreamIdle();

void onChronosConnection(bool connected) {
  (void)connected;
  chronosConnectionEvent = true;
}

void onChronosConfig(Config config, uint32_t a, uint32_t b) {
  (void)a;
  (void)b;
  if (config == CF_NAV_DATA || config == CF_NAV_ICON) {
    // Do not touch OLED from the BLE callback.
    lastNavPoll = 0;
  }
}

void setupChronosCallbacks() {
  if (chronosCallbacksReady) return;
  chronosWatch.setConnectionCallback(onChronosConnection);
  chronosWatch.setConfigurationCallback(onChronosConfig);
  chronosCallbacksReady = true;
}

void updateChronosBLE() {
  if (!bleEnabled) return;
  chronosWatch.loop();
  const bool connected = chronosWatch.isConnected();
  if (connected != bleConnected || chronosConnectionEvent) {
    chronosConnectionEvent = false;
    if (connected != bleConnected) {
      bleConnected = connected;
      Serial.println(connected ? "[CHRONOS] Phone connected" : "[CHRONOS] Phone disconnected");
      resetScreenCache();
    }
  }

  unsigned long now = millis();
  if (lastNavPoll != 0 && now - lastNavPoll < 350UL) return;
  lastNavPoll = now;

  Navigation &nav = chronosWatch.getNavigation();
  bool active = connected && nav.active;
  bool changed = active != navActive;
  if (active) {
    changed = changed || navTitle != nav.title || navDirections != nav.directions ||
              navDistance != nav.distance || navEta != nav.eta ||
              navHasIcon != nav.hasIcon || navIconCRC != nav.iconCRC;
    if (changed) {
      navTitle = nav.title;
      navDirections = nav.directions;
      navDistance = nav.distance;
      navEta = nav.eta;
      navHasIcon = nav.hasIcon;
      navIconCRC = nav.iconCRC;
      Serial.print("[CHRONOS] NAV: ");
      Serial.print(navTitle);
      Serial.print(" | ");
      Serial.println(navDirections);
    }
  }
  // Automatically show turn-by-turn directions when Chronos starts navigation.
  // BLE remains manual-only; this does not start BLE or enable phone notifications.
  const bool wasNavActive = navActive;
  navActive = active;

  if (navActive && !wasNavActive) {
    Serial.println("[CHRONOS] Navigation started - auto opening Maps");
    currentMode = MODE_NAVIGATION;
    resetScreenCache();
  } else if (!navActive && wasNavActive) {
    resetDreamIdle();
    Serial.println("[CHRONOS] Navigation ended");
    if (currentMode == MODE_NAVIGATION) {
      currentMode = MODE_HOME;
      resetScreenCache();
    }
  } else if (navActive && changed && currentMode == MODE_NAVIGATION) {
    resetScreenCache();
  }
}

// --- ALIEN SIGNAL STATE ---
String currentAlienMessage = "BELUM ADA SINYAL...";
String lastAlienLog1 = "Sektor 0: Stabil";
String lastAlienLog2 = "Sektor 7: Sunyi";
unsigned long alienSignalStartedAt = 0;
const unsigned long ALIEN_SIGNAL_DURATION_MS = 8000UL;

// --- RADAR SAVER STATE ---
unsigned long radarStartedAt = 0;
const unsigned long RADAR_DURATION_MS = 10000UL;
float radarAngle = 0.0;

// --- IDLE MUMBLE STATE ---
unsigned long lastTouchOrActionAt = 0;
unsigned long nextIdleMumbleDelay = 180000UL;

// --- WEATHER CACHE STRUCT ---
struct WeatherData {
  float temp = 0.0;
  int humidity = 0;
  int aqi = 0;
  float uvIndex = 0.0;
  int precipitationProb = 0;
  int weatherCode = 0;
  bool isFetched = false;
} cachedWeather;

const char* settingItems[] = {
  "Sound FX",         // 0
  "Chronos BLE",      // 1
  "Google Maps",      // 2
  "System Monitor",   // 3
  "Weather P'pinang", // 4
  "Deep Sleep",       // 5
  "Personality",      // 6
  "OLED Brightness",  // 7
  "Dream Mode",       // 8
  "WiFi Setup",       // 9
  "OTA Update",       // 10
  "Zevron Info",      // 11
  "Light Mode",       // 12
  "Games",            // 13
  "Exit Home"         // 14
};

const int SETTINGS_COUNT = sizeof(settingItems) / sizeof(settingItems[0]);
int settingsIndex = 0;

// ============================================================
// HELPER UTILITIES
// ============================================================
String extractJsonString(const String& json, const String& key, int fromIndex = 0) {
  String needle = "\"" + key + "\"";
  int keyPos = json.indexOf(needle, fromIndex);
  if (keyPos < 0) return "";
  int colon = json.indexOf(':', keyPos + needle.length());
  if (colon < 0) return "";
  int firstQuote = json.indexOf('"', colon + 1);
  if (firstQuote < 0) return "";
  int endQuote = json.indexOf('"', firstQuote + 1);
  if (endQuote < 0) return "";
  return json.substring(firstQuote + 1, endQuote);
}

// ============================================================
// DREAM MODE: 5 min -> sleepy; +3 min -> sleep; dream every 30s
// OLED-only, ESP32 keeps BLE and on-demand WiFi functionality.
// ============================================================
const unsigned long DREAM_SLEEPY_AFTER_MS = 300000UL;
const unsigned long DREAM_SLEEP_AFTER_MS  = 480000UL;
const unsigned long DREAM_INTERVAL_MS     = 30000UL;
const unsigned long DREAM_FRAME_MS        = 130UL;
bool dreamSleeping = false;
bool dreamSleepyApplied = false;
bool dreamWakeConsume = false;
unsigned long dreamStartedAt = 0;
unsigned long dreamLastFrame = 0;
uint8_t dreamTheme = 0;

void resetDreamIdle() {
  lastTouchOrActionAt = millis();
  dreamSleeping = false;
  dreamSleepyApplied = false;
}

void drawDreamScreen() {
  unsigned long now = millis();
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Gentle closed eyes and breathing animation
  int breath = (now / 850UL) % 2;
  display.drawRoundRect(20, 27 + breath, 36, 9, 4, SSD1306_WHITE);
  display.drawRoundRect(72, 27 + breath, 36, 9, 4, SSD1306_WHITE);
  display.fillRect(21, 27 + breath, 34, 4, SSD1306_BLACK);
  display.fillRect(73, 27 + breath, 34, 4, SSD1306_BLACK);
  display.drawPixel(62, 43 + breath, SSD1306_WHITE);
  display.setCursor(109, 43);
  display.print("zZ");

  unsigned long elapsed = now - dreamStartedAt;
  // Show a short dream vignette at each 30-second interval
  if ((elapsed % DREAM_INTERVAL_MS) < 8000UL) {
    if ((elapsed / DREAM_INTERVAL_MS) != dreamTheme) {
      dreamTheme = (elapsed / DREAM_INTERVAL_MS) % 4;
    }
    display.drawCircle(91, 12, 10, SSD1306_WHITE);
    display.drawCircle(79, 23, 2, SSD1306_WHITE);
    display.drawPixel(75, 26, SSD1306_WHITE);
    switch (dreamTheme % 4) {
      case 0: // Moon
        display.fillCircle(91, 12, 5, SSD1306_WHITE);
        display.fillCircle(94, 10, 5, SSD1306_BLACK);
        break;
      case 1: // Star
        display.drawLine(91, 5, 91, 19, SSD1306_WHITE);
        display.drawLine(84, 12, 98, 12, SSD1306_WHITE);
        break;
      case 2: // Planet
        display.drawCircle(91, 12, 5, SSD1306_WHITE);
        display.drawLine(82, 16, 100, 8, SSD1306_WHITE);
        break;
      default: // Heart
        display.fillCircle(88, 10, 3, SSD1306_WHITE);
        display.fillCircle(94, 10, 3, SSD1306_WHITE);
        display.fillTriangle(85, 12, 97, 12, 91, 19, SSD1306_WHITE);
        break;
    }
  }
  display.setCursor(2, 56);
  display.print("Dreaming... touch wake");
  display.display();
}

void updateDreamMode() {
  unsigned long now = millis();
  if (navActive) return; // Navigation has priority
  if (currentMode != MODE_HOME && currentMode != MODE_DREAM) return;

  unsigned long idle = now - lastTouchOrActionAt;
  if (currentMode == MODE_HOME && idle >= DREAM_SLEEP_AFTER_MS) {
    dreamSleeping = true;
    dreamStartedAt = now;
    dreamLastFrame = 0;
    dreamTheme = 0;
    currentMode = MODE_DREAM;
    resetScreenCache();
    Serial.println("[DREAM] Sleeping");
  } else if (currentMode == MODE_HOME && idle >= DREAM_SLEEPY_AFTER_MS) {
    // Keep RoboEyes alive, only change expression
    if (!dreamSleepyApplied) {
      roboEyes.setMood(TIRED);
      dreamSleepyApplied = true;
      Serial.println("[DREAM] Sleepy");
    }
  }

  if (currentMode == MODE_DREAM && now - dreamLastFrame >= DREAM_FRAME_MS) {
    dreamLastFrame = now;
    drawDreamScreen();
  }
}

// ============================================================
// ZEVRON MINI GAMES - single TTP223, non-blocking
// Menu: 1 tap switch, 2 taps select, long press exit.
// During game: tap action, hold >= 900ms exit to Games.
// ============================================================
uint8_t gamesIndex = 0;
unsigned long gameFrameAt = 0;
unsigned long gameStartedAt = 0;
bool gameOver = false;
uint16_t gameScore = 0;
uint16_t gameBestDino = 0;
uint16_t gameBestFish = 0;

// Dino: fixed-point Y, positive velocity means downward
int16_t dinoY10 = 0, dinoV10 = 0;
int16_t obstacleX = 128;
uint8_t obstacleW = 8;
uint8_t obstacleH = 11;

// Fishing: hook moves automatically, tap to cast at the right time
int16_t fishX = 10;
int8_t fishSpeed = 2;
bool fishCasting = false;
int16_t hookY = 15;
uint8_t fishMisses = 0;

void startGame(bool dino) {
  currentMode = dino ? MODE_DINO_GAME : MODE_FISH_GAME;
  gameFrameAt = 0;
  gameStartedAt = millis();
  gameOver = false;
  gameScore = 0;
  dinoY10 = 0; dinoV10 = 0;
  obstacleX = 128; obstacleW = 8; obstacleH = 11;
  fishX = 10; fishSpeed = 2; fishCasting = false; hookY = 15;
  fishMisses = 0;
  resetScreenCache();
}

void gameTapAction() {
  if (gameOver) {
    startGame(currentMode == MODE_DINO_GAME);
    return;
  }
  if (currentMode == MODE_DINO_GAME && dinoY10 == 0) {
    dinoV10 = -39;
    playSelectSound();
  } else if (currentMode == MODE_FISH_GAME && !fishCasting) {
    fishCasting = true;
    hookY = 15;
    playSelectSound();
  }
}

void drawGamesMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(24, 1); display.print("ZEVRON GAMES");
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);
  display.setCursor(4, 20);
  display.print(gamesIndex == 0 ? "> " : "  ");
  display.print("Dino Runner");
  display.setCursor(4, 34);
  display.print(gamesIndex == 1 ? "> " : "  ");
  display.print("Fishing Zevron");
  display.setCursor(2, 54); display.print("1:Next 2:Play Hold:Exit");
  display.display();
}

void drawGameOver(const char* title, uint16_t best) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 2); display.print(title);
  display.setTextSize(2);
  display.setCursor(8, 17); display.print("GAME OVER");
  display.setTextSize(1);
  display.setCursor(10, 40); display.print("Score: "); display.print(gameScore);
  display.setCursor(72, 40); display.print("Best:"); display.print(best);
  display.setCursor(6, 55); display.print("Tap:Retry Hold:Menu");
  display.display();
}

void updateDinoGame(unsigned long now) {
  if (now - gameFrameAt < 45UL) return;
  gameFrameAt = now;
  if (gameOver) { drawGameOver("DINO RUNNER", gameBestDino); return; }

  if (dinoV10 || dinoY10 < 0) {
    dinoY10 += dinoV10;
    dinoV10 += 4;
    if (dinoY10 >= 0) { dinoY10 = 0; dinoV10 = 0; }
  }
  obstacleX -= 3 + min((int)gameScore / 6, 3);
  if (obstacleX < -obstacleW) {
    obstacleX = 128 + random(5, 30);
    obstacleH = random(8, 16);
    ++gameScore;
    if (gameScore > gameBestDino) gameBestDino = gameScore;
  }
  int dinoTop = 49 + dinoY10 / 10;
  if (obstacleX < 25 && obstacleX + obstacleW > 12 && dinoTop + 11 > 53 - obstacleH) {
    gameOver = true;
    playErrorSound();
    drawGameOver("DINO RUNNER", gameBestDino);
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("DINO ");
  display.print(gameScore);
  display.setCursor(82, 0); display.print("BEST:");
  display.print(gameBestDino);
  display.drawLine(0, 60, 127, 60, SSD1306_WHITE);
  display.fillRect(13, dinoTop, 12, 11, SSD1306_WHITE);
  display.drawPixel(21, dinoTop + 3, SSD1306_BLACK);
  display.fillRect(obstacleX, 60 - obstacleH, obstacleW, obstacleH, SSD1306_WHITE);
  display.display();
}

void updateFishGame(unsigned long now) {
  if (now - gameFrameAt < 55UL) return;
  gameFrameAt = now;
  if (gameOver) { drawGameOver("FISHING", gameBestFish); return; }
  fishX += fishSpeed;
  if (fishX < 8 || fishX > 115) { fishSpeed = -fishSpeed; fishX += fishSpeed; }
  if (fishCasting) {
    hookY += 3;
    if (hookY >= 47) {
      fishCasting = false;
      hookY = 15;
      // Fish moves along waterline; landing hook near fish catches it.
      if (abs(fishX - 64) <= 12) {
        ++gameScore;
        if (gameScore > gameBestFish) gameBestFish = gameScore;
        playConfirmSound();
      } else {
        ++fishMisses;
        if (fishMisses >= 5) { gameOver = true; playErrorSound(); }
      }
    }
  }
  if (gameOver) { drawGameOver("FISHING", gameBestFish); return; }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("FISH ");
  display.print(gameScore);
  display.setCursor(77, 0); display.print("MISS ");
  display.print(fishMisses); display.print("/5");
  display.drawLine(0, 22, 127, 22, SSD1306_WHITE);
  display.drawLine(64, 14, 64, fishCasting ? hookY : 15, SSD1306_WHITE);
  display.drawCircle(64, fishCasting ? hookY : 15, 2, SSD1306_WHITE);
  // Fish swims horizontally; align the cast with the center target.
  display.fillTriangle(fishX - 6, 47, fishX + 6, 42, fishX + 6, 52, SSD1306_WHITE);
  display.fillTriangle(fishX + 6, 47, fishX + 11, 43, fishX + 11, 51, SSD1306_WHITE);
  display.drawRect(51, 54, 26, 6, SSD1306_WHITE);
  display.setCursor(0, 56); display.print("CAST!");
  display.display();
}

void updateMiniGames() {
  unsigned long now = millis();
  if (currentMode == MODE_DINO_GAME) updateDinoGame(now);
  else if (currentMode == MODE_FISH_GAME) updateFishGame(now);
}

// ============================================================
// TOUCH SYSTEM
// ============================================================
bool touchRawPrevious = false;
bool touchStable = false;
bool touchLongHandled = false;

unsigned long touchChangedAt = 0;
unsigned long touchPressedAt = 0;
unsigned long lastTapAt = 0;
unsigned long lastTouchActionAt = 0;

const unsigned long TOUCH_DEBOUNCE_MS = 45;
const unsigned long TAP_GAP_MS = 380;
const unsigned long LONG_PRESS_MS = 900;
const unsigned long TOUCH_LOCKOUT_MS = 100;

int tapCount = 0;

// ============================================================
// BUZZER & CUSTOM RANDOM MELODIES
// ============================================================
MelodyNote melodyNotes[8];
uint8_t melodyLength = 0;
uint8_t melodyIndex = 0;

bool melodyPlaying = false;
bool buzzerActive = false;

unsigned long buzzerEndsAt = 0;
unsigned long nextMelodyNoteAt = 0;

void stopMelody() {
  melodyPlaying = false;
  buzzerActive = false;
  melodyLength = 0;
  melodyIndex = 0;
  noTone(BUZZER_PIN);
}

void startMelody(const MelodyNote* notes, uint8_t count) {
  stopMelody();
  if (!soundOn || count == 0) return;
  if (count > 8) count = 8;
  for (uint8_t i = 0; i < count; i++) { melodyNotes[i] = notes[i]; }
  melodyLength = count;
  melodyIndex = 0;
  melodyPlaying = true;
  nextMelodyNoteAt = millis();
}

void updateBuzzer() {
  unsigned long now = millis();
  if (buzzerActive && (long)(now - buzzerEndsAt) >= 0) {
    noTone(BUZZER_PIN);
    buzzerActive = false;
  }
  if (!melodyPlaying || (long)(now - nextMelodyNoteAt) < 0) return;
  if (melodyIndex >= melodyLength) { stopMelody(); return; }

  MelodyNote note = melodyNotes[melodyIndex++];
  if (note.frequency > 0 && soundOn) {
    tone(BUZZER_PIN, note.frequency);
    buzzerActive = true;
  } else {
    noTone(BUZZER_PIN);
    buzzerActive = false;
  }
  buzzerEndsAt = now + note.duration;
  nextMelodyNoteAt = now + note.duration + 18;
}

void playAlienSignalSound() {
  const MelodyNote n[] = {{200, 120}, {150, 150}, {300, 200}, {120, 250}, {0, 0}};
  startMelody(n, 4);
}

void playRandomTapSound() {
  int variant = random(0, 4);
  if (variant == 0) {
    const MelodyNote n[] = {{523, 75}, {659, 75}, {784, 100}, {0, 0}};
    startMelody(n, 3);
  } else if (variant == 1) {
    const MelodyNote n[] = {{880, 60}, {988, 60}, {1046, 120}, {0, 0}};
    startMelody(n, 3);
  } else if (variant == 2) {
    const MelodyNote n[] = {{440, 90}, {554, 90}, {659, 120}, {0, 0}};
    startMelody(n, 3);
  } else {
    const MelodyNote n[] = {{659, 80}, {523, 80}, {784, 140}, {0, 0}};
    startMelody(n, 3);
  }
}

void playSelectSound() {
  const MelodyNote n[] = {{784, 65}, {988, 65}, {1175, 65}, {1568, 120}};
  startMelody(n, 4);
}

void playBackSound() {
  const MelodyNote n[] = {{1047, 80}, {880, 80}, {698, 130}};
  startMelody(n, 3);
}

void playConfirmSound() {
  const MelodyNote n[] = {{659, 60}, {784, 60}, {988, 60}, {1319, 150}};
  startMelody(n, 4);
}

void playErrorSound() {
  const MelodyNote n[] = {{440, 100}, {349, 150}, {262, 200}};
  startMelody(n, 3);
}

// ============================================================
// ZEVRON INFO SCREEN
// ============================================================
String getHardwareMacAddress() {
  WiFi.mode(WIFI_STA);
  String mac = WiFi.macAddress();
  WiFi.mode(WIFI_OFF);
  return mac;
}

void drawInfoScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("ZEVRON SYSTEM INFO");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 14); display.print("Ver   : "); display.println(ZEVRON_FIRMWARE_VERSION);
  display.setCursor(0, 26); display.print("Creator: ZDYVOID");
  display.setCursor(0, 38); display.print("MAC   : "); display.println(getHardwareMacAddress());

  display.setCursor(0, 55); display.print("Tap any key: Back");
  display.display();
}

// ============================================================
// ALIEN SIGNAL
// ============================================================
void triggerAlienSignal() {
  if (currentMode != MODE_HOME) return;

  const char* alienMessages[] = {
    "Kopi lu dikasih serum\npengontrol otak!",
    "Piring terbang kami\nparkir di atap kosan.",
    "Warning: Cicak tembok\nitu CCTV rahasia!",
    "Sinyal bawah tanah\nsektor 7 terdeteksi.",
    "Pimpinan dunia\nsebenarnya reptil, bro.",
    "Solder lu membakar\njalur galaksi lain!",
    "Jangan lihat ke atas\njam 3 pagi nanti...",
    "Rekaman rahasia Area 51\nberhasil bocor.",
    "Manusia di kamar ini\ncuma simulasi Matrix.",
    "Satelit kami sedang\nmembaca pikiran lu.",
    "Ibu Kapal (Mothership)\nsudah mendekat!",
    "Kabel USB lu disusupi\njin berdimensi 4.",
    "Bumi sebenarnya chip\nESP32 raksasa.",
    "Mereka ada di dalam\nBulan... diam-diam.",
    "Zevron mendeteksi portal\nalien di meja!",
    "Pemadaman sinyal global\nakan dimulai.",
    "Burung itu bukan hewan,\ntapi drone live!",
    "Eksperimen penculikan\nmanusia ditunda.",
    "Frekuensi otak lu\npenuh dengan statik.",
    "Kebenaran telah\ndisembunyikan negara.",
    "Pusat kendali alien:\nSinyal terkunci!",
    "Pintu dimensi paralel\nterbuka di kosan.",
    "Awas! Jaringan Wi-Fi\nini disadap UFO.",
    "Ketiak manusia mengandung\nenergi OMEGA.",
    "Mothership minta kiriman\nkopi kapal api.",
    "Protokol perang galaksi\nsedang dimuat...",
    "Jaringan satelit\nterisolasi total!",
    "Suhu ruangan terlalu\ntinggi bagi alien.",
    "Sinyal terputus...\nMereka datang!",
    "Kael Node sedang\ndisadap oleh satelit."
  };

  lastAlienLog2 = lastAlienLog1;
  lastAlienLog1 = currentAlienMessage;

  currentAlienMessage = String(alienMessages[random(0, 30)]);
  playAlienSignalSound();
  alienSignalStartedAt = millis();
  currentMode = MODE_ALIEN_SIGNAL;
  resetScreenCache();
}

// ============================================================
// DESKTOP RADAR SWEEP ANIMATION
// ============================================================
void triggerRadarSaver() {
  if (currentMode != MODE_HOME) return;
  radarStartedAt = millis();
  currentMode = MODE_RADAR_SAVER;
  resetScreenCache();
}

void drawRadarScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  int cx = 96;
  int cy = 32;
  int r = 26;
  display.drawCircle(cx, cy, r, SSD1306_WHITE);
  display.drawCircle(cx, cy, r / 2, SSD1306_WHITE);
  display.drawLine(cx - r, cy, cx + r, cy, SSD1306_WHITE);
  display.drawLine(cx, cy - r, cx, cy + r, SSD1306_WHITE);

  radarAngle += 0.15;
  int sx = cx + (int)(cos(radarAngle) * r);
  int sy = cy + (int)(sin(radarAngle) * r);
  display.drawLine(cx, cy, sx, sy, SSD1306_WHITE);

  display.setCursor(2, 4); display.print("RADAR SCAN");
  display.setCursor(2, 18); display.print("SECTOR: 09");
  display.setCursor(2, 32); display.print("ANOMALY: DETECT");
  display.setCursor(2, 52); display.print("[Tap] Wake Up");

  display.display();
}

void triggerIdleMumble() {
  if (currentMode != MODE_HOME) return;
  int moodChoice = random(0, 3);
  if (moodChoice == 0) roboEyes.setMood(TIRED);
  else if (moodChoice == 1) roboEyes.setMood(HAPPY);
  else roboEyes.setMood(DEFAULT);
  
  int posChoice = random(0, 3);
  if (posChoice == 0) roboEyes.setPosition(ZORA_EYE_POSITION_EAST);
  else if (posChoice == 1) roboEyes.setPosition(ZORA_EYE_POSITION_WEST);
  else roboEyes.setPosition(DEFAULT);
}

// ============================================================
// SCREEN CACHE & SETTINGS
// ============================================================
AppMode lastDrawnMode = (AppMode)255;
int lastDrawnSettingsIndex = -1;
bool lastDrawnSound = true;
bool lastDrawnBle = false;

String otaLatestVersion = "Unknown";
String otaFirmwareUrl = "";
String otaStatusMessage = "Tap 1x to check";
bool otaReleaseFound = false;
bool happyActive = false;
unsigned long happyStartedAt = 0;
const unsigned long HAPPY_DURATION_MS = 5000UL;

void applyOledBrightness() {
  display.ssd1306_command(0x81);
  display.ssd1306_command(oledBrightness);
}

void resetScreenCache() {
  lastDrawnMode = (AppMode)255;
  lastDrawnSettingsIndex = -1;
  lastDrawnSound = !soundOn;
  lastDrawnBle = !bleEnabled;
}

void playSlideTransition(bool slideLeft) {
  for (int x = 0; x < OLED_WIDTH; x += 16) {
    if (slideLeft) display.startscrollleft(0x00, 0x0F);
    else display.startscrollright(0x00, 0x0F);
  }
  delay(60);
  display.stopscroll();
}

void applyPersonalitySettings() {
  switch (zevronPersonality) {
    case PERS_CHILL:
      roboEyes.setAutoblinker(ON, 3, 2); 
      roboEyes.setIdleMode(ON, 2, 2); 
      roboEyes.setMood(DEFAULT); 
      break;
    case PERS_ENERGETIC:
      roboEyes.setAutoblinker(ON, 1, 1); 
      roboEyes.setIdleMode(ON, 1, 1); 
      roboEyes.setMood(DEFAULT); 
      break;
    case PERS_TSUNDERE:
      roboEyes.setAutoblinker(ON, 4, 2); 
      roboEyes.setIdleMode(ON, 2, 3); 
      roboEyes.setMood(ANGRY); 
      break;
    case PERS_ROBOT:
      roboEyes.setAutoblinker(OFF, 0, 0); 
      roboEyes.setIdleMode(ON, 4, 4); 
      roboEyes.setMood(DEFAULT); 
      break;
  }
}

void saveSettings() {
  preferences.begin("zevron", false);
  preferences.putBool("sound", soundOn);
  preferences.putBool("light", lightModeEnabled);
  preferences.putInt("bright", oledBrightness);
  preferences.putInt("pers", (int)zevronPersonality);
  preferences.end();
}

void loadSettings() {
  preferences.begin("zevron", true);
  soundOn = preferences.getBool("sound", true);
  lightModeEnabled = preferences.getBool("light", false);
  oledBrightness = preferences.getInt("bright", 255);
  zevronPersonality = (Personality)preferences.getInt("pers", (int)PERS_CHILL);
  preferences.end();
}

void showBootSplash() {
  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.drawRect(2, 2, 124, 60, SSD1306_WHITE);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2); display.setCursor(14, 14); display.println("ZEVRON");
  display.setTextSize(1); display.setCursor(32, 38); display.println("BY ZDYVOID");
  display.display();
  delay(1500);
  display.clearDisplay();
  display.display();
}

void returnHome() {
  resetDreamIdle();
  playSlideTransition(false);
  currentMode = MODE_HOME;
  tapCount = 0;
  applyPersonalitySettings();
  resetScreenCache();
  playBackSound();
}

// ============================================================
// WIFI & WEATHER FETCH
// ============================================================
bool connectWiFiOnDemand() {
  if (WiFi.status() == WL_CONNECTED) return true;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 15); display.println("WIFI ON-DEMAND");
  display.setCursor(0, 32); display.println("Connecting & Sync...");
  display.display();

  WiFi.mode(WIFI_STA);
  esp_wifi_set_max_tx_power(36);
  WiFi.begin();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo;
    unsigned long ntpWait = millis();
    while (!getLocalTime(&timeinfo, 100) && millis() - ntpWait < 3000UL) delay(100);
  }
  return (WiFi.status() == WL_CONNECTED);
}

void disconnectWiFiPowerSave() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void fetchWeatherPangkalpinang() {
  if (!connectWiFiOnDemand()) { cachedWeather.isFetched = false; return; }
  HTTPClient http;
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(LATITUDE, 4) + 
               "&longitude=" + String(LONGITUDE, 4) + "&current_weather=true&hourly=uv_index,precipitation_probability";
  http.begin(url);
  if (http.GET() == HTTP_CODE_OK) {
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, http.getString());
    cachedWeather.temp = doc["current_weather"]["temperature"];
    cachedWeather.weatherCode = doc["current_weather"]["weathercode"];
    cachedWeather.uvIndex = doc["hourly"]["uv_index"][0];
    cachedWeather.precipitationProb = doc["hourly"]["precipitation_probability"][0];
    cachedWeather.humidity = 78;
    cachedWeather.aqi = 28;
    cachedWeather.isFetched = true;
  }
  http.end();
  disconnectWiFiPowerSave();
}

String getWeatherConditionText(int code) {
  if (code == 0) return "Cerah (Clear)";
  if (code >= 1 && code <= 3) return "Berawan (Cloudy)";
  if (code >= 45 && code <= 48) return "Berkabut (Fog)";
  if (code >= 51 && code <= 67) return "Hujan Ringan (Rain)";
  return "Normal";
}

void drawWeatherOnDemandScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("P'PINANG WEATHER");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  if (cachedWeather.isFetched) {
    display.setCursor(0, 13); display.print("Cond : "); display.print(getWeatherConditionText(cachedWeather.weatherCode));
    display.setCursor(0, 23); display.printf("Temp : %.1f C\n", cachedWeather.temp);
    display.setCursor(0, 33); display.printf("UV   : %.1f | AQI: %d\n", cachedWeather.uvIndex, cachedWeather.aqi);
    display.setCursor(0, 43); display.printf("Rain : %d%%\n", cachedWeather.precipitationProb);
    display.setCursor(0, 55); display.print("[Tap] Refresh");
  } else {
    display.setCursor(0, 25); display.print("Fetching Data...");
  }
  display.display();
}

void drawAlienLogScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("ALIEN CONTACT LOG");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  
  display.setCursor(0, 13); display.print("1>"); display.println(lastAlienLog1.substring(0, 19));
  display.setCursor(0, 31); display.print("2>"); display.println(lastAlienLog2.substring(0, 19));
  
  display.setCursor(0, 55); display.print("Tap: Back");
  display.display();
}

void drawSystemMonitorScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("SYS MONITOR");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 16); display.print("Free RAM : "); display.print(ESP.getFreeHeap()); display.print(" B");
  display.setCursor(0, 28); display.print("Uptime   : "); display.print(millis() / 60000); display.print("m");
  display.setCursor(0, 40); display.print("WiFi RSSI: "); 
  if (WiFi.status() == WL_CONNECTED) { display.print(WiFi.RSSI()); display.print(" dBm"); }
  else { display.print("Off/Disconn"); }
  display.setCursor(0, 55); display.print("Tap: Home");
  display.display();
}

void drawClockScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("ZEVRON CLOCK");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 50)) {
    display.setCursor(10, 25); display.println("Time not synced!");
  } else {
    char timeStr[9];
    sprintf(timeStr, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    display.setTextSize(2); display.setCursor(15, 18); display.print(timeStr);
  }
  display.setCursor(0, 57); display.print("Tap:Home");
  display.display();
}

void drawModernSettings() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 1); display.print("ZEVRON CONTROL");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  int topIndex = settingsIndex - 1;
  if (topIndex < 0) topIndex = 0;
  if (topIndex > SETTINGS_COUNT - 3) topIndex = SETTINGS_COUNT - 3;

  for (int i = 0; i < 3; i++) {
    int idx = topIndex + i;
    if (idx >= SETTINGS_COUNT) break;
    int y = 13 + (i * 16);
    if (idx == settingsIndex) {
      display.fillRoundRect(0, y, 128, 15, 3, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.drawRoundRect(0, y, 128, 15, 3, SSD1306_WHITE);
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(6, y + 4);
    display.print(idx == settingsIndex ? "> " : "  ");
    display.print(settingItems[idx]);
  }
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 61); display.print("1x:Down  2x:Select");
  display.display();
}

void drawAlienSignalScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("[*] DEEP SPACE SIGNAL");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 16); display.print("Freq: 1420.4 MHz");
  display.setCursor(0, 27); display.println(currentAlienMessage);
  display.setCursor(0, 55); display.print("[Tap] Dismiss Signal");
  display.display();
}





// LAYAR STATUS SOUND FX
void drawSoundScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("SOUND SETTINGS");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);
  display.setTextSize(2); display.setCursor(18, 25); display.println(soundOn ? "SOUND ON" : "MUTE OFF");
  display.setTextSize(1); display.setCursor(2, 54); display.print("1x:Toggle 2x+:Back");
  display.display();
}

// Chronos navigation is a separate data stream from ordinary phone notifications.
// Display the native 48x48 maneuver bitmap when Chronos provides it.
void drawNavigationScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("GOOGLE MAPS / BLE");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if (!bleEnabled) {
    display.setCursor(0, 20); display.println("BLE is OFF");
    display.setCursor(0, 33); display.println("Enable in Settings");
  } else if (!bleConnected) {
    display.setCursor(0, 20); display.println("Waiting for Chronos");
    display.setCursor(0, 33); display.println("Connect from phone");
  } else if (!navActive) {
    display.setCursor(0, 20); display.println("Chronos connected");
    display.setCursor(0, 33); display.println("Start Maps route");
  } else {
    Navigation &nav = chronosWatch.getNavigation();
    if (navHasIcon) {
      for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
          int i = (y * 48 + x) / 8;
          int bit = 7 - (x % 8);
          if ((nav.icon[i] >> bit) & 1) display.drawPixel(x, y + 12, SSD1306_WHITE);
        }
      }
    }
    const int left = navHasIcon ? 52 : 0;
    const int chars = navHasIcon ? 12 : 21;
    display.setCursor(left, 14);
    display.println(navTitle.substring(0, chars));
    display.setCursor(left, 25);
    display.println(navDirections.substring(0, chars));
    display.setCursor(left, 36);
    display.println(navDirections.substring(chars, chars * 2));
    display.setCursor(left, 49);
    display.println(navEta.substring(0, chars));
  }
  display.display();
}

void drawBleStatusScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("CHRONOS BLE STATUS");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);
  display.setTextSize(2);
  if (!bleEnabled) {
    display.setCursor(18, 23); display.println("BLE OFF");
  } else if (bleConnected) {
    display.setCursor(6, 23); display.println("CONNECTED");
  } else {
    display.setCursor(18, 23); display.println("BLE ON");
    display.setTextSize(1);
    display.setCursor(10, 43); display.println("Waiting for phone");
  }
  display.setTextSize(1);
  display.setCursor(2, 55); display.print("1x:Toggle 2x+:Back");
  display.display();
}

void drawPersonalityStatusScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("ROBO PERSONALITY");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);
  
  display.setTextSize(2); 
  display.setCursor(10, 25); 
  if (zevronPersonality == PERS_CHILL) display.println("CHILL");
  else if (zevronPersonality == PERS_ENERGETIC) display.println("ENERGETIC");
  else if (zevronPersonality == PERS_TSUNDERE) display.println("TSUNDERE");
  else display.println("ROBOT");

  display.setTextSize(1);
  display.setCursor(2, 54); display.print("1x:Change 2x+:Back");
  display.display();
}

// LAYAR STATUS OLED BRIGHTNESS
void drawBrightnessStatusScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("OLED BRIGHTNESS");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);
  
  display.setTextSize(2); 
  display.setCursor(8, 25); 
  if (oledBrightness > 200) display.println("HIGH (255)");
  else if (oledBrightness > 50) display.println("MED (128)");
  else display.println("LOW (30)");

  display.setTextSize(1);
  display.setCursor(2, 54); display.print("1x:Cycle 2x+:Back");
  display.display();
}

// LAYAR STATUS LIGHT MODE
void drawLightModeScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("LIGHT MODE");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);
  
  display.setTextSize(2); 
  display.setCursor(14, 25); 
  display.println(lightModeEnabled ? "LIGHT ON" : "DARK OFF");

  display.setTextSize(1);
  display.setCursor(2, 54); display.print("1x:Toggle 2x+:Back");
  display.display();
}

void toggleSound() { 
  soundOn = !soundOn; 
  saveSettings(); 
  playConfirmSound(); 
  currentMode = MODE_SOUND_STATUS;
  resetScreenCache(); 
}

void toggleBrightness() {
  oledBrightness = (oledBrightness > 200) ? 30 : (oledBrightness > 50) ? 128 : 255;
  applyOledBrightness(); 
  saveSettings(); 
  playConfirmSound(); 
  currentMode = MODE_BRIGHTNESS_STATUS;
  resetScreenCache();
}

void toggleLightMode() {
  lightModeEnabled = !lightModeEnabled;
  if (lightModeEnabled) {
    display.ssd1306_command(SSD1306_INVERTDISPLAY);
  } else {
    display.ssd1306_command(SSD1306_NORMALDISPLAY);
  }
  saveSettings();
  playConfirmSound();
  currentMode = MODE_LIGHT_STATUS;
  resetScreenCache();
}

void cyclePersonality() {
  zevronPersonality = (Personality)((zevronPersonality + 1) % 4);
  applyPersonalitySettings(); 
  saveSettings(); 
  playConfirmSound(); 
  currentMode = MODE_PERSONALITY_STATUS;
  resetScreenCache();
}

void toggleChronosBLE() {
  if (bleEnabled) {
    Serial.println("[CHRONOS] Disabling BLE...");
    chronosWatch.stop(true);
    bleEnabled = false;
    bleConnected = false;
    chronosConnectionEvent = false;
    navActive = false;
    navTitle = "";
    navDirections = "";
    navDistance = "";
    navEta = "";
    Serial.println("[CHRONOS] BLE OFF");
    playBackSound();
  } else {
    setupChronosCallbacks();
    if (!chronosCallbacksReady) {
      Serial.println("[CHRONOS] Cannot enable BLE: callbacks not ready");
      playBackSound();
    } else {
      chronosWatch.begin();
      bleEnabled = chronosWatch.isRunning();
      bleConnected = bleEnabled && chronosWatch.isConnected();
      Serial.println(bleEnabled ? "[CHRONOS] BLE ON: ZEVRON-ZDY" : "[CHRONOS] BLE failed to start");
      if (bleEnabled) playConfirmSound();
      else playBackSound();
    }
  }
  currentMode = MODE_BLE_STATUS;
  resetScreenCache();
}

bool checkLatestGitHubRelease() {
  otaStatusMessage = "Connecting WiFi...";
  resetScreenCache();
  if (!connectWiFiOnDemand()) {
    otaStatusMessage = "WiFi Failed";
    resetScreenCache();
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setTimeout(12000);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!https.begin(client, GITHUB_RELEASE_API)) {
    otaStatusMessage = "HTTPS failed";
    disconnectWiFiPowerSave(); resetScreenCache(); return false;
  }
  https.addHeader("User-Agent", GITHUB_USER_AGENT);
  https.addHeader("Accept", "application/vnd.github+json");
  int code = https.GET();

  if (code != HTTP_CODE_OK) {
    otaStatusMessage = "HTTP " + String(code);
    https.end(); disconnectWiFiPowerSave(); resetScreenCache(); return false;
  }
  String body = https.getString();
  https.end();

  String tag = extractJsonString(body, "tag_name");
  int assetsPos = body.indexOf("\"assets\"");
  String assetUrl = "";
  if (assetsPos >= 0) {
    int searchAt = assetsPos;
    while (true) {
      int urlPos = body.indexOf("\"browser_download_url\"", searchAt);
      if (urlPos < 0) break;
      String candidate = extractJsonString(body, "browser_download_url", urlPos);
      if (candidate.endsWith(".bin")) { assetUrl = candidate; break; }
      searchAt = urlPos + 22;
    }
  }

  disconnectWiFiPowerSave();

  if (tag.length() == 0 || assetUrl.length() == 0) {
    otaReleaseFound = false;
    otaStatusMessage = "No .bin asset";
    otaLatestVersion = tag.length() ? tag : "Unknown";
    resetScreenCache();
    return false;
  }
  otaLatestVersion = tag;
  otaFirmwareUrl = assetUrl;
  otaReleaseFound = true;
  otaStatusMessage = (tag == String(ZEVRON_FIRMWARE_VERSION) || tag == String("v") + ZEVRON_FIRMWARE_VERSION)
      ? "Latest version" : "Update available";
  resetScreenCache();
  return true;
}

void installLatestFirmware() {
  if (!otaReleaseFound || otaFirmwareUrl.length() == 0) {
    otaStatusMessage = "Check release first";
    resetScreenCache();
    return;
  }
  if (!connectWiFiOnDemand()) {
    otaStatusMessage = "WiFi Failed";
    resetScreenCache();
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setTimeout(20000);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!https.begin(client, otaFirmwareUrl)) {
    otaStatusMessage = "HTTPS failed";
    disconnectWiFiPowerSave(); resetScreenCache(); return;
  }
  https.addHeader("User-Agent", GITHUB_USER_AGENT);
  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    otaStatusMessage = "Download HTTP " + String(code);
    https.end(); disconnectWiFiPowerSave(); resetScreenCache(); return;
  }
  int contentLength = https.getSize();
  if (contentLength <= 0 || !Update.begin(contentLength)) {
    otaStatusMessage = "Invalid size";
    https.end(); disconnectWiFiPowerSave(); resetScreenCache(); return;
  }

  otaStatusMessage = "Downloading...";
  resetScreenCache();
  WiFiClient* stream = https.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  bool updateOK = (written == (size_t)contentLength) && Update.end() && Update.isFinished();
  https.end();
  disconnectWiFiPowerSave();

  if (updateOK) {
    display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(0, 18); display.println("OTA SUCCESS");
    display.setCursor(0, 34); display.println("Restarting ZEVRON..."); display.display();
    delay(900);
    ESP.restart();
  } else {
    Update.abort();
    otaStatusMessage = "OTA failed";
    resetScreenCache();
  }
}

void drawOTAScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("FIRMWARE UPDATE");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 14); display.print("Current: "); display.println(ZEVRON_FIRMWARE_VERSION);
  display.setCursor(0, 25); display.print("Latest:  "); display.println(otaLatestVersion);
  display.setCursor(0, 38); display.println(otaStatusMessage.substring(0, 21));
  display.setCursor(0, 50); display.println("1:Check  2:Install");
  display.setCursor(0, 59); display.println("3+ : Back");
  display.display();
}

void openWiFiPortal() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10); display.println("WIFI SETUP PORTAL");
  display.setCursor(0, 26); display.println("Connect Wi-Fi to:");
  display.setCursor(0, 38); display.println("mochibot_zaldy");
  display.setCursor(0, 52); display.println("192.168.4.1");
  display.display();
  wm.setTimeout(180);
  wm.startConfigPortal("mochibot_zaldy");
  disconnectWiFiPowerSave();
  resetScreenCache();
}

void triggerManualDeepSleep() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(18, 25);
  display.setTextSize(1);
  display.println("Entering Sleep...");
  display.display();
  delay(800);
  
  display.clearDisplay();
  display.setCursor(38, 28);
  display.setTextSize(2);
  display.print("-_- zZ");
  display.display();
  
  currentMode = MODE_SLEEP;
  isDimmed = true;
  display.dim(true);
}

void selectSetting() {
  switch (settingsIndex) {
    case 0: currentMode = MODE_SOUND_STATUS; resetScreenCache(); playConfirmSound(); break;  // Sound FX
    case 1: currentMode = MODE_BLE_STATUS; resetScreenCache(); playSelectSound(); break;      // Chronos BLE (view only)
    case 2: currentMode = MODE_NAVIGATION; resetScreenCache(); playSelectSound(); break;     // Google Maps
    case 3: currentMode = MODE_SYS_MONITOR; resetScreenCache(); playConfirmSound(); break;   // System Monitor
    case 4: currentMode = MODE_WEATHER_ONDEMAND; fetchWeatherPangkalpinang(); resetScreenCache(); playConfirmSound(); break; // Weather
    case 5: triggerManualDeepSleep(); break;                                                 // Deep Sleep
    case 6: currentMode = MODE_PERSONALITY_STATUS; resetScreenCache(); playConfirmSound(); break; // Personality
    case 7: currentMode = MODE_BRIGHTNESS_STATUS; resetScreenCache(); playConfirmSound(); break; // OLED Brightness
    case 8: currentMode = MODE_DREAM; dreamSleeping = true; dreamStartedAt = millis(); dreamLastFrame = 0; resetScreenCache(); playConfirmSound(); break; // Dream preview
    case 9: openWiFiPortal(); break;                                                         // WiFi Setup
    case 10: currentMode = MODE_OTA; otaStatusMessage = "Tap 1x to check"; resetScreenCache(); playConfirmSound(); break; // OTA Update
    case 11: currentMode = MODE_ZEVRON_INFO; resetScreenCache(); playConfirmSound(); break;  // Zevron Info
    case 12: currentMode = MODE_LIGHT_STATUS; resetScreenCache(); playConfirmSound(); break; // Light Mode
    case 13: currentMode = MODE_GAMES_MENU; gamesIndex = 0; resetScreenCache(); playConfirmSound(); break;
    case 14: returnHome(); break;                                                            // Exit Home
  }
}

void handleTapCount(int count) {
  resetDreamIdle();
  if (currentMode == MODE_ALIEN_SIGNAL || currentMode == MODE_RADAR_SAVER) { returnHome(); return; }
  if (currentMode == MODE_ZEVRON_INFO) { currentMode = MODE_SETTINGS; resetScreenCache(); playBackSound(); return; }

  switch (currentMode) {
    case MODE_HOME:
      if (count == 3) { currentMode = MODE_CLOCK; resetScreenCache(); playSelectSound(); }
      else if (count == 4) { currentMode = MODE_WEATHER_ONDEMAND; fetchWeatherPangkalpinang(); resetScreenCache(); playSelectSound(); }
      else if (count == 5) { currentMode = MODE_NAVIGATION; resetScreenCache(); playSelectSound(); }
      else if (count == 8) { currentMode = MODE_ALIEN_LOG; resetScreenCache(); playConfirmSound(); }
      else if (count >= 9) { currentMode = MODE_SETTINGS; settingsIndex = 0; resetScreenCache(); playConfirmSound(); }
      break;
    case MODE_SETTINGS:
      if (count == 1) { settingsIndex = (settingsIndex + 1) % SETTINGS_COUNT; resetScreenCache(); playRandomTapSound(); }
      else if (count == 2) { selectSetting(); }
      else { returnHome(); }
      break;
    case MODE_SOUND_STATUS:
      if (count == 1) { toggleSound(); } 
      else { currentMode = MODE_SETTINGS; resetScreenCache(); playBackSound(); }
      break;
    case MODE_BLE_STATUS:
      if (count == 1) { toggleChronosBLE(); } 
      else { currentMode = MODE_SETTINGS; resetScreenCache(); playBackSound(); }
      break;
    case MODE_PERSONALITY_STATUS:
      if (count == 1) { cyclePersonality(); } 
      else { currentMode = MODE_SETTINGS; resetScreenCache(); playBackSound(); }
      break;
    case MODE_BRIGHTNESS_STATUS:
      if (count == 1) { toggleBrightness(); } 
      else { currentMode = MODE_SETTINGS; resetScreenCache(); playBackSound(); }
      break;
    case MODE_LIGHT_STATUS:
      if (count == 1) { toggleLightMode(); }
      else { currentMode = MODE_SETTINGS; resetScreenCache(); playBackSound(); }
      break;
    case MODE_GAMES_MENU:
      if (count == 1) { gamesIndex ^= 1; resetScreenCache(); playSelectSound(); }
      else if (count == 2) { startGame(gamesIndex == 0); }
      else { returnHome(); }
      break;
    case MODE_DINO_GAME:
    case MODE_FISH_GAME:
      // Gameplay action is handled on touch-down to avoid tap-gap latency.
      break;
    case MODE_DREAM:
    case MODE_SYS_MONITOR:
    case MODE_NAVIGATION:
    case MODE_ALIEN_LOG:
      if (count >= 1) { currentMode = MODE_SETTINGS; resetScreenCache(); playBackSound(); }
      break;
    case MODE_WEATHER_ONDEMAND:
      if (count >= 2) { fetchWeatherPangkalpinang(); resetScreenCache(); }
      else { currentMode = MODE_SETTINGS; resetScreenCache(); playBackSound(); }
      break;
    case MODE_OTA:
      if (count == 1) { checkLatestGitHubRelease(); }
      else if (count == 2) { installLatestFirmware(); }
      else { currentMode = MODE_SETTINGS; resetScreenCache(); playBackSound(); }
      break;
    default: returnHome(); break;
  }
}

void handleLongPress() {
  resetDreamIdle();
  if (currentMode == MODE_DINO_GAME || currentMode == MODE_FISH_GAME) {
    currentMode = MODE_GAMES_MENU;
    resetScreenCache();
    playBackSound();
    return;
  }
  if (currentMode == MODE_GAMES_MENU) { returnHome(); return; }
  if (currentMode == MODE_ZEVRON_INFO) {
    currentMode = MODE_SETTINGS;
    resetScreenCache();
    return;
  }
  if (currentMode == MODE_HOME) {
    happyActive = false;
    roboEyes.setMood(ANGRY);
    roboEyes.setPosition(DEFAULT);
    playErrorSound();
    return;
  }
  returnHome();
}

void updateTouch() {
  unsigned long now = millis();
  bool raw = digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL;
  if (raw != touchRawPrevious) { touchRawPrevious = raw; touchChangedAt = now; }
  if (now - touchChangedAt >= TOUCH_DEBOUNCE_MS && raw != touchStable) {
    touchStable = raw;
    if (touchStable) {
      touchPressedAt = now; touchLongHandled = false; lastTouchOrActionAt = now;
      if (currentMode == MODE_DINO_GAME || currentMode == MODE_FISH_GAME) {
        // Tap immediately acts in game; do not queue normal menu gestures.
        tapCount = 0;
        gameTapAction();
        return;
      }
      if (currentMode == MODE_DREAM) {
        dreamWakeConsume = true;
        dreamSleeping = false;
        currentMode = MODE_HOME;
        applyPersonalitySettings();
        resetScreenCache();
        return;
      }
      if (currentMode == MODE_SLEEP || currentMode == MODE_RADAR_SAVER || currentMode == MODE_ALIEN_SIGNAL) {
        returnHome();
        tone(BUZZER_PIN, 1200, 50);
        return;
      }
      if (currentMode == MODE_HOME) {
        playRandomTapSound();
        
        roboEyes.setMood(HAPPY);
        int randomPos = random(0, 3);
        if (randomPos == 0) roboEyes.setPosition(ZORA_EYE_POSITION_EAST);
        else if (randomPos == 1) roboEyes.setPosition(ZORA_EYE_POSITION_WEST);
        else roboEyes.setPosition(DEFAULT);

        happyActive = true; 
        happyStartedAt = now;
      }
    } else if (currentMode == MODE_DINO_GAME || currentMode == MODE_FISH_GAME) {
      tapCount = 0;
    } else if (dreamWakeConsume) {
      dreamWakeConsume = false;
      tapCount = 0;
    } else if (!touchLongHandled && now - lastTouchActionAt >= TOUCH_LOCKOUT_MS) {
      lastTouchActionAt = now; tapCount++; lastTapAt = now;
    }
  }
  if (touchStable && !touchLongHandled && now - touchPressedAt >= LONG_PRESS_MS) {
    touchLongHandled = true; tapCount = 0; handleLongPress();
  }
  if (tapCount > 0 && now - lastTapAt > TAP_GAP_MS) {
    int count = tapCount; tapCount = 0; handleTapCount(count);
  }
}

void renderCurrentScreen() {
  switch (currentMode) {
    case MODE_HOME: roboEyes.update(); break;
    case MODE_SLEEP:
      display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
      display.setCursor(38, 28); display.setTextSize(2); display.print("-_- zZ");
      display.display();
      break;
    case MODE_ALIEN_SIGNAL: drawAlienSignalScreen(); break;
    case MODE_RADAR_SAVER: drawRadarScreen(); break;
    case MODE_ZEVRON_INFO: drawInfoScreen(); break;
    case MODE_SETTINGS:
      if (lastDrawnMode != MODE_SETTINGS || lastDrawnSettingsIndex != settingsIndex) {
        drawModernSettings(); lastDrawnSettingsIndex = settingsIndex;
      }
      break;
    case MODE_SOUND_STATUS: drawSoundScreen(); break;
    case MODE_BLE_STATUS: drawBleStatusScreen(); break;
    case MODE_PERSONALITY_STATUS: drawPersonalityStatusScreen(); break;
    case MODE_BRIGHTNESS_STATUS: drawBrightnessStatusScreen(); break;
    case MODE_LIGHT_STATUS: drawLightModeScreen(); break;
    case MODE_NAVIGATION: drawNavigationScreen(); break;
    case MODE_CLOCK: drawClockScreen(); break;
    case MODE_WEATHER_ONDEMAND: drawWeatherOnDemandScreen(); break;
    case MODE_ALIEN_LOG: drawAlienLogScreen(); break;
    case MODE_SYS_MONITOR: drawSystemMonitorScreen(); break;
    case MODE_GAMES_MENU: if (lastDrawnMode != MODE_GAMES_MENU || lastDrawnSettingsIndex != gamesIndex) {
        drawGamesMenu(); lastDrawnSettingsIndex = gamesIndex;
      } break;
    case MODE_DINO_GAME:
    case MODE_FISH_GAME: break; // updateMiniGames draws at capped FPS
    case MODE_DREAM: break; // updateDreamMode renders at limited FPS
    case MODE_OTA: if (lastDrawnMode != MODE_OTA) drawOTAScreen(); break;
    default: break;
  }
  lastDrawnMode = currentMode;
}

void setup() {
  Serial.begin(115200);
  delay(150);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  
  WiFi.mode(WIFI_STA);
  delay(100);
  Serial.print("ZEVRON MAC : "); Serial.println(WiFi.macAddress());

  loadSettings();
  
  // ==========================================
  // PASTIKAN BLE SELALU OFF SAAT BOOTING (100% MANUAL)
  // ==========================================
  bleEnabled = false;
  bleConnected = false;
  setupChronosCallbacks(); 
  Serial.println("[CHRONOS] Boot: BLE OFF (enable manually in Settings)");

  applyOledBrightness();
  
  if (lightModeEnabled) {
    display.ssd1306_command(SSD1306_INVERTDISPLAY);
  } else {
    display.ssd1306_command(SSD1306_NORMALDISPLAY);
  }

  showBootSplash();
  WiFi.mode(WIFI_OFF);

  roboEyes.begin(OLED_WIDTH, OLED_HEIGHT, 100);
  applyPersonalitySettings();
  roboEyes.setPosition(DEFAULT);

  randomSeed(micros());
  lastTouchOrActionAt = millis();
  nextIdleMumbleDelay = random(120000UL, 240000UL);
  resetScreenCache();
}

void loop() {
  updateChronosBLE();
  updateTouch();
  updateDreamMode();
  updateMiniGames();
  unsigned long now = millis();

  // Random idle interruptions disabled so Dream Mode has uninterrupted idle timing.

  if (currentMode == MODE_ALIEN_SIGNAL && (now - alienSignalStartedAt >= ALIEN_SIGNAL_DURATION_MS)) {
    returnHome();
  }

  if (currentMode == MODE_RADAR_SAVER && (now - radarStartedAt >= RADAR_DURATION_MS)) {
    returnHome();
  }

  if (happyActive && currentMode == MODE_HOME && now - happyStartedAt >= HAPPY_DURATION_MS) {
    happyActive = false;
    applyPersonalitySettings();
  }

  updateBuzzer();
  renderCurrentScreen();
  delay(1);
}