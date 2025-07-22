// Debug flags
#define DEBUG_BUTTONS 1  // Enable button debugging
#define ENABLE_TELNET_DEBUG 1  // Enable telnet debugging (port 23)

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

#if ENABLE_TELNET_DEBUG
// Telnet server for wireless debugging
WiFiServer telnetServer(23);
WiFiClient telnetClient;
#endif

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

// ========================================================
// PIXEL CHASER GAME VARIABLES
// ========================================================
int playerIndex = -1;
int playerIndexTrail = -1;
int playerDirection = 1;
unsigned long nextMove = 0;
float currentPlayerSpeed = 150;
unsigned long countdown = 0;
int enemyIndex = -1;
int coinIndex = -1;
int score = 0;
int bestScore = 0;
int lastScore = 0;
bool gameOver = false;
bool gameActive = false;
unsigned long gameOverAnimationStart = 0;
bool gameOverAnimationRunning = false;
unsigned long lastDirectionChange = 0;
#define DIRECTION_CHANGE_DEBOUNCE 200

// Forward declarations for pattern functions
void rainbow();
void rainbowWithGlitter();
void addGlitter(fract8 chanceOfGlitter);
void confetti();
void sinelon();
void bpm();
void juggle();
void pixelChaserGame();
void nextPattern();
void prevPattern();

// Debug helper functions
void debugPrint(String message);
void debugPrintf(const char* format, ...);

// ========================================================
// PIXEL CHASER GAME FUNCTIONS
// ========================================================
void initializeGame();
void clearLevel();
void setLevel();
void displayPlayer();
void gameOverAnimation();
void showBestScore();
void toggleGameState();
void changePlayerDirection();

// Pattern list definition
typedef void (*SimplePatternList[])();
SimplePatternList gPatterns = { rainbow, rainbowWithGlitter, confetti, sinelon, juggle, bpm, pixelChaserGame };

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

  // Debug endpoint for wireless debugging
  server.on("/debug", []() {
    String debugInfo = "<html><head><title>Debug Console</title>"
                       "<meta http-equiv='refresh' content='2'></head>"
                       "<body style='font-family: monospace; background: black; color: green; padding: 20px;'>"
                       "<h2>🔧 Live Debug Console</h2>"
                       "<div style='background: #111; padding: 15px; border: 1px solid #333;'>";
    
    debugInfo += "<p><strong>System Status:</strong></p>";
    debugInfo += "Pattern: " + String(gCurrentPatternNumber) + " (Game=" + String(gCurrentPatternNumber == 6 ? "YES" : "NO") + ")<br>";
    debugInfo += "Game Active: " + String(gameActive ? "YES" : "NO") + "<br>";
    debugInfo += "Game Over: " + String(gameOver ? "YES" : "NO") + "<br>";
    debugInfo += "Score: " + String(score) + " | Best: " + String(bestScore) + "<br>";
    debugInfo += "Player Pos: " + String(playerIndex) + " | Enemy: " + String(enemyIndex) + " | Coin: " + String(coinIndex) + "<br>";
    debugInfo += "Speed: " + String(currentPlayerSpeed) + "ms<br>";
    debugInfo += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes<br>";
    debugInfo += "WiFi RSSI: " + String(WiFi.RSSI()) + " dBm<br>";
    debugInfo += "Uptime: " + String(millis() / 1000) + " seconds<br>";
    
    debugInfo += "<p><strong>Button States:</strong></p>";
    debugInfo += "Yellow: " + String(digitalRead(BUTTON_YELLOW_PIN) ? "HIGH" : "LOW") + " | ";
    debugInfo += "Red: " + String(digitalRead(BUTTON_RED_PIN) ? "HIGH" : "LOW") + " | ";
    debugInfo += "Blue: " + String(digitalRead(BUTTON_BLUE_PIN) ? "HIGH" : "LOW") + " | ";
    debugInfo += "Green: " + String(digitalRead(BUTTON_GREEN_PIN) ? "HIGH" : "LOW") + "<br>";
    
    debugInfo += "</div><p><a href='/'>← Back to Main</a></p></body></html>";
    
    server.send(200, "text/html", debugInfo);
  });
 
  server.on("/", []() {
    String patternNames[] = {"Rainbow", "Rainbow+Glitter", "Confetti", "Sinelon", "Juggle", "BPM", "Pixel Chaser Game"};
    String currentPatternName = (gCurrentPatternNumber < 7) ? patternNames[gCurrentPatternNumber] : "Unknown";
    
    String gameStatus = "";
    if (gCurrentPatternNumber == 6) { // Pixel Chaser Game pattern
      gameStatus = "<h2>🎮 Pixel Chaser Game Status</h2>"
                   "<p><strong>Game Active:</strong> " + String(gameActive ? "Yes" : "No") + "</p>"
                   "<p><strong>Current Score:</strong> " + String(score) + "</p>"
                   "<p><strong>Best Score:</strong> " + String(bestScore) + "</p>"
                   "<p><strong>Game Over:</strong> " + String(gameOver ? "Yes" : "No") + "</p>"
                   "<h3>🎮 Game Controls:</h3>"
                   "<ul>"
                   "<li><strong>Yellow/Green Button:</strong> Change player direction</li>"
                   "<li><strong>Red Button:</strong> Start/Stop game</li>"
                   "<li><strong>Blue Button:</strong> Reset best score</li>"
                   "</ul>";
    }
    
    String html = "<html><head><title>Suitcase Game Board</title></head>"
                  "<body style='font-family: Arial, sans-serif; margin: 40px;'>"
                  "<h1>🎲 Suitcase Game Board</h1>"
                  "<h2>📊 Status Information</h2>"
                  "<p><strong>WiFi Status:</strong> Connected ✅</p>"
                  "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>"
                  "<p><strong>Hostname:</strong> suitcase-game.local</p>"
                  "<p><strong>Current Pattern:</strong> " + String(gCurrentPatternNumber) + " (" + currentPatternName + ")</p>"
                  "<p><strong>LED Brightness:</strong> " + String(FastLED.getBrightness()) + "</p>"
                  "<p><strong>Free Heap:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>"
                  + gameStatus +
                  "<h2>🎮 Button Controls</h2>"
                  "<ul>"
                  "<li><strong>Yellow Button:</strong> Next pattern / Change direction (in game)</li>"
                  "<li><strong>Red Button:</strong> Brightness up / Start/Stop game (in game)</li>"
                  "<li><strong>Blue Button:</strong> Brightness down / Reset best score (in game)</li>"
                  "<li><strong>Green Button:</strong> Previous pattern / Change direction (in game)</li>"
                  "</ul>"
                  "<h2>🔧 Actions</h2>"
                  "<p><a href='/update' style='background-color: #4CAF50; color: white; padding: 10px 20px; text-decoration: none; border-radius: 4px; margin-right: 10px;'>🔄 OTA Update</a>"
                  "<a href='/debug' style='background-color: #FF9800; color: white; padding: 10px 20px; text-decoration: none; border-radius: 4px;'>🔧 Debug Console</a></p>"
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

#if ENABLE_TELNET_DEBUG
  // Start Telnet server for wireless debugging
  telnetServer.begin();
  Serial.println("Telnet debug server started on port 23");
  Serial.println("Connect with: telnet " + WiFi.localIP().toString() + " 23");
#endif
  
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

#if ENABLE_TELNET_DEBUG
    // Handle telnet debug connections
    if (telnetServer.hasClient()) {
      if (telnetClient && telnetClient.connected()) {
        telnetServer.available().stop();
      } else {
        telnetClient = telnetServer.available();
        if (telnetClient) {
          telnetClient.println("🔧 ESP32 Debug Console Connected");
          telnetClient.printf("Device: suitcase-game.local (%s)\n", WiFi.localIP().toString().c_str());
          telnetClient.println("Commands: 'status' for info, 'game' for game debug");
          telnetClient.print("> ");
        }
      }
    }
    
    // Handle telnet client commands
    if (telnetClient && telnetClient.connected() && telnetClient.available()) {
      String command = telnetClient.readStringUntil('\n');
      command.trim();
      
      if (command == "status") {
        telnetClient.printf("Pattern: %d, Game: %s, Active: %s, Score: %d/%d\n", 
                           gCurrentPatternNumber, (gCurrentPatternNumber == 6 ? "YES" : "NO"),
                           gameActive ? "YES" : "NO", score, bestScore);
      } else if (command == "game") {
        telnetClient.printf("Game Debug - Player: %d, Enemy: %d, Coin: %d, Speed: %.0f\n",
                           playerIndex, enemyIndex, coinIndex, currentPlayerSpeed);
      }
      telnetClient.print("> ");
    }
#endif
  }

  // Button handling with debounce
  if (currentMillis - lastButtonCheck >= DEBOUNCE_TIME) {
    lastButtonCheck = currentMillis;
    
    yellow_button_state = digitalRead(BUTTON_YELLOW_PIN);
    red_button_state = digitalRead(BUTTON_RED_PIN);
    blue_button_state = digitalRead(BUTTON_BLUE_PIN);
    green_button_state = digitalRead(BUTTON_GREEN_PIN);

    // Check if we're in the pixel chaser game pattern (pattern #6 is the game)
    bool inGamePattern = (gCurrentPatternNumber == 6);
    
    #if DEBUG_BUTTONS
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 5000) { // Debug every 5 seconds
      Serial.printf("Current pattern: %d, Game pattern index: %d, In game: %s\n", 
                    gCurrentPatternNumber, (sizeof(gPatterns) / sizeof(gPatterns[0])) - 1, 
                    inGamePattern ? "YES" : "NO");
      lastDebugTime = millis();
    }
    #endif
    
    if (yellow_prev_button_state == HIGH && yellow_button_state == LOW) {
      digitalWrite(LED_YELLOW, HIGH);
      if (inGamePattern) {
        // In game: Yellow button changes direction
        changePlayerDirection();
        Serial.println("Player direction changed");
      } else {
        // Normal mode: Next pattern
        nextPattern();
        Serial.println("Next pattern");
      }
      lastMillis = currentMillis;
    }

    if (red_prev_button_state == HIGH && red_button_state == LOW) {
      digitalWrite(LED_RED, HIGH);
      
      #if DEBUG_BUTTONS
      debugPrintf("Red button pressed! Pattern: %d, InGame: %s\n", 
                  gCurrentPatternNumber, inGamePattern ? "YES" : "NO");
      #endif
      
      if (inGamePattern) {
        // In game: Red button starts/stops game (long press equivalent)
        debugPrint("Calling toggleGameState()\n");
        toggleGameState();
        debugPrint("Game toggled\n");
      } else {
        // Normal mode: Brightness up
        int current = FastLED.getBrightness();
        if (current < 255) {
          FastLED.setBrightness(min(current + 25, 255));
        }
        Serial.println("Brightness up");
      }
      lastMillis = currentMillis;
    }

    if (blue_prev_button_state == HIGH && blue_button_state == LOW) {
      digitalWrite(LED_BLUE, HIGH);
      if (inGamePattern) {
        // In game: Blue button resets best score
        bestScore = 0;
        Serial.println("Best score reset");
      } else {
        // Normal mode: Brightness down
        int current = FastLED.getBrightness();
        if (current > 0) {
          FastLED.setBrightness(max(current - 25, 0));
        }
        Serial.println("Brightness down");
      }
      lastMillis = currentMillis;
    }

    if (green_prev_button_state == HIGH && green_button_state == LOW) {
      #if DEBUG_BUTTONS
      Serial.println("Green button pressed!");
      Serial.printf("States: prev=%d, current=%d\n", green_prev_button_state, green_button_state);
      #endif
      
      digitalWrite(LED_GREEN, HIGH);
      if (inGamePattern) {
        // In game: Green button changes direction (alternative to yellow)
        changePlayerDirection();
        Serial.println("Player direction changed (green)");
      } else {
        // Normal mode: Previous pattern
        prevPattern();
        Serial.println("Previous pattern");
      }
      lastMillis = currentMillis;
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
// PIXEL CHASER GAME IMPLEMENTATION
// =================================================

void pixelChaserGame() {
  // Initialize game if just entering this pattern
  static bool gameInitialized = false;
  if (!gameInitialized) {
    initializeGame();
    gameInitialized = true;
  }
  
  // Reset initialization flag when leaving this pattern
  static uint8_t lastPatternNumber = 255;
  if (lastPatternNumber != gCurrentPatternNumber) {
    if (lastPatternNumber == (sizeof(gPatterns) / sizeof(gPatterns[0])) - 1) {
      gameInitialized = false;
    }
    lastPatternNumber = gCurrentPatternNumber;
  }
  
  // If game over animation is running, handle it
  if (gameOverAnimationRunning) {
    gameOverAnimation();
    return;
  }
  
  // If the game is in game over state, show best score
  if (gameOver) {
    showBestScore();
    return;
  }
  
  // Set the pixel display state of the level
  setLevel();
  
  // Wait for countdown before player appears
  if (countdown > millis()) {
    return;
  }
  
  // All the player display, movement and game logic
  if (gameActive) {
    displayPlayer();
  }
}

void initializeGame() {
  playerIndex = -1;
  playerIndexTrail = -1;
  playerDirection = 1;
  nextMove = 0;
  currentPlayerSpeed = 150;
  countdown = millis() + 2000;
  enemyIndex = -1;
  coinIndex = -1;
  score = 0;
  gameOver = false;
  gameActive = false;
  gameOverAnimationRunning = false;
  clearLevel();
}

void clearLevel() {
  FastLED.clear();
}

void setLevel() {
  // If the enemy position is -1 (has been reset)
  // Find a new position for the enemy
  if (enemyIndex < 0) {
    // If the player not playing, always start the enemy at the half strip position
    if (playerIndex < 0) {
      enemyIndex = NUM_LEDS / 2;
    }
    // The player is in the game, so make sure not to place the enemy on or too close to the player
    else {
      enemyIndex = random(0, NUM_LEDS);
      while (abs(enemyIndex - playerIndex) < (NUM_LEDS / 4)) {
        enemyIndex = random(0, NUM_LEDS);
      }
    }
  }
  
  // If the coin position is -1 (has been reset)
  // Find a new position for the coin
  if (coinIndex < 0) {
    coinIndex = random(0, NUM_LEDS);
    // Pick a coin position somewhere between the player and enemy
    while (abs(coinIndex - playerIndex) < 3 || abs(coinIndex - enemyIndex) < 3) {
      coinIndex = random(0, NUM_LEDS);
    }
  }
  
  // Set enemy and coin colors
  if (enemyIndex >= 0 && enemyIndex < NUM_LEDS) {
    leds[enemyIndex] = CRGB::Red;
  }
  if (coinIndex >= 0 && coinIndex < NUM_LEDS) {
    leds[coinIndex] = CRGB::Yellow;
  }
}

void displayPlayer() {
  if (nextMove < millis()) {
    nextMove = millis() + currentPlayerSpeed;
    
    // The player has a visual trail, so clear the trail
    if (playerIndexTrail >= 0 && playerIndexTrail < NUM_LEDS) {
      leds[playerIndexTrail] = CRGB::Black;
    }
    
    if (playerIndex >= 0 && playerIndex < NUM_LEDS) {
      leds[playerIndex] = CRGB(0, 100, 0); // Dim green for trail
      playerIndexTrail = playerIndex;
    }
    
    // Move the player in their current direction
    playerIndex += playerDirection;
    
    // Wrap the player at the strip edges
    if (playerIndex < 0) {
      playerIndex = NUM_LEDS - 1;
    } else if (playerIndex >= NUM_LEDS) {
      playerIndex = 0;
    }
    
    // Set bright green for current player position
    leds[playerIndex] = CRGB::Green;
    
    // Did the player hit the coin?
    if (playerIndex == coinIndex) {
      enemyIndex = -1;
      coinIndex = -1;
      score++;
      currentPlayerSpeed = constrain(currentPlayerSpeed - 10, 50, 150);
      clearLevel();
      leds[playerIndex] = CRGB::Green;
      Serial.printf("Score: %d\n", score);
    }
    // Did the player hit the enemy?
    else if (playerIndex == enemyIndex) {
      lastScore = score;
      if (score >= bestScore) {
        bestScore = score;
      }
      
      Serial.printf("Game Over! Score: %d, Best: %d\n", score, bestScore);
      
      // Start game over animation
      gameOverAnimationStart = millis();
      gameOverAnimationRunning = true;
    }
  }
}

void gameOverAnimation() {
  unsigned long elapsed = millis() - gameOverAnimationStart;
  
  if (elapsed < 1000) {
    // Flash red animation from enemy position
    int animationProgress = (elapsed / 20) % (NUM_LEDS / 2);
    clearLevel();
    
    for (int i = 0; i <= animationProgress; i++) {
      int pos1 = (enemyIndex + i) % NUM_LEDS;
      int pos2 = (enemyIndex - i + NUM_LEDS) % NUM_LEDS;
      leds[pos1] = CRGB::Red;
      if (pos2 != pos1) {
        leds[pos2] = CRGB::Red;
      }
    }
  } else if (elapsed < 1500) {
    // Clear animation
    clearLevel();
  } else {
    // Animation complete
    gameOverAnimationRunning = false;
    gameOver = true;
    gameActive = false;
    enemyIndex = -1;
    coinIndex = -1;
    playerIndex = -1;
  }
}

void showBestScore() {
  clearLevel();
  
  // Show best score in yellow
  for (int i = 0; i < NUM_LEDS && i < bestScore; i++) {
    leds[i] = CRGB(255, 155, 0); // Yellow-orange
  }
  
  // Show last score in red if less than best
  if (lastScore < bestScore) {
    for (int i = 0; i < NUM_LEDS && i < lastScore; i++) {
      leds[i] = CRGB::Red;
    }
  }
}

void toggleGameState() {
  debugPrintf("toggleGameState called! Current state - gameOver: %s, gameActive: %s\n", 
              gameOver ? "true" : "false", gameActive ? "true" : "false");
  
  gameOver = !gameOver;
  gameOverAnimationRunning = false;
  
  if (gameOver) {
    // Stop the game
    gameActive = false;
    enemyIndex = -1;
    coinIndex = -1;
    playerIndex = -1;
    currentPlayerSpeed = 150;
    clearLevel();
    debugPrint("Game stopped\n");
    
    // LED indicator: Game stopped (blink red 3 times)
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_RED, HIGH);
      delay(100);
      digitalWrite(LED_RED, LOW);
      delay(100);
    }
  } else {
    // Start the game
    gameActive = true;
    clearLevel();
    score = 0;
    currentPlayerSpeed = 150;
    countdown = millis() + 2000;
    debugPrint("Game started - 2 second countdown\n");
    
    // LED indicator: Game started (blink green 3 times)
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_GREEN, HIGH);
      delay(100);
      digitalWrite(LED_GREEN, LOW);
      delay(100);
    }
  }
  
  debugPrintf("New state - gameOver: %s, gameActive: %s\n", 
              gameOver ? "true" : "false", gameActive ? "true" : "false");
}

void changePlayerDirection() {
  // Debounce direction changes
  if (millis() - lastDirectionChange < DIRECTION_CHANGE_DEBOUNCE) {
    return;
  }
  
  // No input until player is visible and game is active
  if (countdown > millis() || !gameActive || gameOver) {
    return;
  }
  
  // Switch the player direction
  playerDirection = -playerDirection;
  lastDirectionChange = millis();
}

// =================================================
// DEBUG HELPER FUNCTIONS
// =================================================

void debugPrint(String message) {
  Serial.print(message);
#if ENABLE_TELNET_DEBUG
  if (telnetClient && telnetClient.connected()) {
    telnetClient.print(message);
  }
#endif
}

void debugPrintf(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  
  Serial.print(buffer);
#if ENABLE_TELNET_DEBUG
  if (telnetClient && telnetClient.connected()) {
    telnetClient.print(buffer);
  }
#endif
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
