// Debug flag
#define DEBUG_BUTTONS 1  // Enable button debugging

// Button pins (before FastLED include)

#include <FastLED.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <ElegantOTA.h>

// Web server and OTA
WebServer server(80);
unsigned long ota_progress_millis = 0;

#define LED_GREEN 5
#define LED_YELLOW 18
#define LED_BLUE 19
#define LED_RED 21

#define BUTTON_YELLOW_PIN 26
#define BUTTON_RED_PIN 27
#define BUTTON_BLUE_PIN 14
#define BUTTON_GREEN_PIN 12

#define DATA_PIN    4
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    16
CRGB leds[NUM_LEDS];

#define BRIGHTNESS         150
#define FRAMES_PER_SECOND  120
#define DEBOUNCE_TIME 50

// Button states
int yellow_button_state = HIGH;
int yellow_prev_button_state = HIGH;
int green_button_state = HIGH;
int green_prev_button_state = HIGH;
int blue_button_state = HIGH;
int blue_prev_button_state = HIGH;
int red_button_state = HIGH;
int red_prev_button_state = HIGH;

// Timing variables
unsigned long lastMillis = 0;
unsigned long lastPatternUpdate = 0;
unsigned long lastButtonCheck = 0;
unsigned long lastHueUpdate = 0;
unsigned long lastWebServerCheck = 0;
unsigned long lastWiFiCheck = 0;

uint8_t gCurrentPatternNumber = 0;
uint8_t gHue = 0;

// Forward declarations for pattern functions
void rainbow();
void rainbowWithGlitter();
void addGlitter(fract8 chanceOfGlitter);
void confetti();
void sinelon();
void bpm();
void juggle();
void nextPattern();
void prevPattern();

// Pattern list definition
typedef void (*SimplePatternList[])();
SimplePatternList gPatterns = { rainbow, rainbowWithGlitter, confetti, sinelon, juggle, bpm };

void setup() {
  // Initialize ESP32 watchdog
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 8000,            // 8 second timeout
    .idle_core_mask = (1 << 0),    // Bitmask of cores to watch (core 0)
    .trigger_panic = true          // Trigger panic when timeout occurs
  };
  esp_task_wdt_init(&wdt_config);  // Initialize with config structure
  esp_task_wdt_add(NULL);          // Add current thread to WDT watch
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting up...");
  
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(LED_RED, LOW);

  pinMode(BUTTON_GREEN_PIN, INPUT_PULLUP);
  pinMode(BUTTON_YELLOW_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BLUE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_RED_PIN, INPUT_PULLUP);

  // ========================================================
  // WiFi and ElegantOTA Setup
  // ========================================================
  
  // Temporarily disable watchdog for WiFi setup (can take long time)
  esp_task_wdt_delete(NULL);
  
  // WiFiManager setup
  WiFiManager wifiManager;
  // Uncomment and run it once, if you want to erase all the stored information
  // wifiManager.resetSettings();
  
  // Set hostname
  wifiManager.setHostname("suitcase-game");
  
  // Set timeout for WiFi connection attempts
  wifiManager.setConfigPortalTimeout(180); // 3 minutes timeout
  
  // fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "SuitcaseGameAP"
  // and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("SuitcaseGameAP");
  
  // if you get here you have connected to the WiFi
  Serial.println("WiFi Connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Re-enable watchdog after WiFi setup
  esp_task_wdt_add(NULL);

  // Set up mDNS responder:
  if (MDNS.begin("suitcase-game")) {
    Serial.println("mDNS started, available at:");
    Serial.println("http://suitcase-game.local/");
  } else {
    Serial.println("Error setting up mDNS responder!");
  }

  // Web server routes
  server.onNotFound([](){ 
    server.send(404, "text/plain", "Link was not found!");  
  });
 
  server.on("/", []() {
    String html = "<html><head><title>Suitcase Game Board</title></head>"
                  "<body style='font-family: Arial, sans-serif; margin: 40px;'>"
                  "<h1>Suitcase Game Board</h1>"
                  "<h2>Status Information</h2>"
                  "<p><strong>WiFi Status:</strong> Connected</p>"
                  "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>"
                  "<p><strong>Hostname:</strong> suitcase-game.local</p>"
                  "<p><strong>Current Pattern:</strong> " + String(gCurrentPatternNumber) + "</p>"
                  "<p><strong>LED Brightness:</strong> " + String(FastLED.getBrightness()) + "</p>"
                  "<p><strong>Free Heap:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>"
                  "<h2>Actions</h2>"
                  "<p><a href='/update' style='background-color: #4CAF50; color: white; padding: 10px 20px; text-decoration: none; border-radius: 4px;'>🔄 OTA Update</a></p>"
                  "<p><em>Note: Device will be temporarily unavailable during OTA update.</em></p>"
                  "</body></html>";
    server.send(200, "text/html", html);
  });

  // Start ElegantOTA
  ElegantOTA.begin(&server);
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  // Start HTTP server
  server.begin();
  Serial.println("HTTP server started");
  
  // Add HTTP service to mDNS
  MDNS.addService("http", "tcp", 80);
  
  // ========================================================

  Serial.println("Setup complete");
}

void loop() {
  unsigned long currentMillis = millis();
  esp_task_wdt_reset(); // Feed the watchdog

  // Check WiFi connection status (every 30 seconds)
  if (currentMillis - lastWiFiCheck >= 30000) {
    lastWiFiCheck = currentMillis;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, attempting to reconnect...");
      WiFi.reconnect();
    }
  }

  // Handle web server and OTA updates (only every 10ms to reduce performance impact)
  if (WiFi.status() == WL_CONNECTED && currentMillis - lastWebServerCheck >= 10) {
    lastWebServerCheck = currentMillis;
    server.handleClient();
    ElegantOTA.loop();
  }

  // Button handling with debounce
  if (currentMillis - lastButtonCheck >= DEBOUNCE_TIME) {
    lastButtonCheck = currentMillis;
    
    yellow_button_state = digitalRead(BUTTON_YELLOW_PIN);
    red_button_state = digitalRead(BUTTON_RED_PIN);
    blue_button_state = digitalRead(BUTTON_BLUE_PIN);
    green_button_state = digitalRead(BUTTON_GREEN_PIN);

    if (yellow_prev_button_state == HIGH && yellow_button_state == LOW) {
      digitalWrite(LED_YELLOW, HIGH);
      nextPattern();
      lastMillis = currentMillis;
      Serial.println("Next pattern");
    }

    if (red_prev_button_state == HIGH && red_button_state == LOW) {
      digitalWrite(LED_RED, HIGH);
      int current = FastLED.getBrightness();
      if (current < 255) {
        FastLED.setBrightness(min(current + 25, 255));
      }
      lastMillis = currentMillis;
      Serial.println("Brightness up");
    }

    if (blue_prev_button_state == HIGH && blue_button_state == LOW) {
      digitalWrite(LED_BLUE, HIGH);
      int current = FastLED.getBrightness();
      if (current > 0) {
        FastLED.setBrightness(max(current - 25, 0));
      }
      lastMillis = currentMillis;
      Serial.println("Brightness down");
    }

    if (green_prev_button_state == HIGH && green_button_state == LOW) {
      #if DEBUG_BUTTONS
      Serial.println("Green button pressed!");
      Serial.printf("States: prev=%d, current=%d\n", green_prev_button_state, green_button_state);
      #endif
      
      digitalWrite(LED_GREEN, HIGH);
      prevPattern();
      lastMillis = currentMillis;
      Serial.println("Previous pattern");
    }

    // Update previous button states
    yellow_prev_button_state = yellow_button_state;
    red_prev_button_state = red_button_state;
    blue_prev_button_state = blue_button_state;
    green_prev_button_state = green_button_state;
  }

  // Turn off LED indicators
  if (currentMillis - lastMillis >= 50) {
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_BLUE, LOW);
    digitalWrite(LED_GREEN, LOW);
  }

  // Update pattern
  if (currentMillis - lastPatternUpdate >= (1000/FRAMES_PER_SECOND)) {
    lastPatternUpdate = currentMillis;
    gPatterns[gCurrentPatternNumber]();
    FastLED.show();
  }

  // Update hue
  if (currentMillis - lastHueUpdate >= 20) {
    lastHueUpdate = currentMillis;
    gHue++;
  }
}

void nextPattern() {
  gCurrentPatternNumber = (gCurrentPatternNumber + 1) % (sizeof(gPatterns) / sizeof(gPatterns[0]));
}

void prevPattern() {
  if (gCurrentPatternNumber == 0) {
    gCurrentPatternNumber = (sizeof(gPatterns) / sizeof(gPatterns[0])) - 1;
  } else {
    gCurrentPatternNumber--;
  }
}

void rainbow() {
  fill_rainbow(leds, NUM_LEDS, gHue, 7);
}

void rainbowWithGlitter() {
  rainbow();
  addGlitter(80);
}

void addGlitter(fract8 chanceOfGlitter) {
  if (random8() < chanceOfGlitter) {
    leds[random16(NUM_LEDS)] += CRGB::White;
  }
}

void confetti() {
  fadeToBlackBy(leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV(gHue + random8(64), 200, 255);
}

void sinelon() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  int pos = beatsin16(13, 0, NUM_LEDS-1);
  leds[pos] += CHSV(gHue, 255, 192);
}

void bpm() {
  uint8_t BeatsPerMinute = 62;
  CRGBPalette16 palette = PartyColors_p;
  uint8_t beat = beatsin8(BeatsPerMinute, 64, 255);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(palette, gHue+(i*2), beat-gHue+(i*10));
  }
}

void juggle() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  uint8_t dothue = 0;
  for (int i = 0; i < 8; i++) {
    leds[beatsin16(i+7, 0, NUM_LEDS-1)] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}

// =================================================
// OTA Callback Functions
// =================================================
void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  Serial.println("Warning: Device will be unresponsive during update");
  
  // Turn off all LEDs during OTA update
  FastLED.clear();
  FastLED.show();
  
  // Turn off status LEDs
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(LED_RED, LOW);
  
  // Temporarily disable watchdog during OTA
  esp_task_wdt_delete(NULL);
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress: %u/%u bytes (%.1f%%)\n", 
                  current, final, ((float)current / (float)final) * 100);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
    Serial.println("Device will restart in 3 seconds...");
    
    // Flash green LED to indicate success
    for (int i = 0; i < 6; i++) {
      digitalWrite(LED_GREEN, HIGH);
      delay(250);
      digitalWrite(LED_GREEN, LOW);
      delay(250);
    }
    
    // Restart device
    ESP.restart();
  } else {
    Serial.println("OTA update failed!");
    Serial.println("Device will restart in 5 seconds...");
    
    // Flash red LED to indicate failure
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_RED, HIGH);
      delay(250);
      digitalWrite(LED_RED, LOW);
      delay(250);
    }
    
    // Restart device even on failure to restore normal operation
    ESP.restart();
  }
}
// =================================================
