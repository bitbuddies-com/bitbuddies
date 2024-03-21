/**
 * Run this code on the ESP8266
 * @author Danny Buonocore, BitBuddies LLC
 */

#include <Wire.h>
#include <LedControl.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include "ESP8266WiFi.h"
#include "ESP8266HTTPClient.h"
#include "ESP8266WebServer.h"
#include "hsv.h"

#define HOST "https://api.bitbuddies.com/"

// Unique id for debugging
String id = "00000000-0000-0000-0000-000000000000";

// Tell the app the color of the display
const String color = "white";
// const String color = "red";
// const String color = "green";
// const String color = "blue";

// BTC
const String endpoint = String(HOST) + String("crypto/BTC");
const String name = "Bitcoin";
const String code = "BTC";

#define BUILD 1
#define DEBUG false

/** @see https://github.com/esp8266/Arduino/blob/master/doc/boards.rst#nodemcu-09-esp-12-module */
#define D0  16
#define D1   5
#define D2   4
#define D3   0
#define D4   2
#define D5  14
#define D6  12
#define D7  13
#define D8  15
#define D9   3
#define D10  1

#define CLK D8
#define CS  D7
#define DIN D0
#define TX  D5
#define RX  D6
#define SDA D1
#define SCL D2
#define PHR A0
#define RST D3

#define ALIGN_LEFT          0
#define ALIGN_RIGHT         1
#define ALIGN_CENTER_LEFT   2
#define ALIGN_CENTER_RIGHT  3

#define LOC_PATTERN             0
#define LOC_RED                 1
#define LOC_GREEN               2
#define LOC_BLUE                3
#define LOC_LED_BRIGHTNESS      4
#define LOC_DISPLAY_BRIGHTNESS  5
#define LOC_AUTO_BRIGHTNESS     6
#define LOC_ALIGNMENT           7
#define LOC_FREQUENCY           8
#define LOC_SESSION             9
#define LOC_DECIMALS           10
#define LOC_CURRENCY           32
#define LOC_SSID               64
#define LOC_PASSWORD          128
#define LOC_PASSWORD_AP       192

#define STATE_PRICE 0   // Default state
#define STATE_PASS  1   // Waiting for new password

SoftwareSerial link(RX, TX);

LedControl ss = LedControl(DIN, CLK, CS, 1);
bool isReady = false;
bool isBlinking = false;

unsigned long previousMillis = 0;
unsigned long previousMillisBlink = 0;
unsigned long previousMillisIntensity = 0;

bool checkIntensity = true;

HTTPClient http;
WiFiClientSecure client;

ESP8266WebServer server(80);
IPAddress ipAP(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

const unsigned long CONNECTION_TIMEOUT = 10000;

const String patterns[2] = { "solid", "fade" };

// Price calculation using an integer base and exponent
int base = 0;
int expo = 0;
int lastBase = 0;
int lastExpo = 0;

int t = 0;
int hue = 0;
int update = 0;

const int INTENSITY_MAX = 64;
int intensityCurrent = 16;
int intensityLast = -1;
int intensityIndex = 0;
int intensity[INTENSITY_MAX];

// Heuristically map the 10 bit brigthtness input to a 4 bit display brightness 
int steps[] = {
  150,  //  64 (1)  + 150 0
  250,  // 128 (3)  + 100 1
  320,  // 196 (5)  + 70  2
  375,  // 256 (6)  + 55  3
  430,  // 320 (7)  + 55  4
  485,  // 384 (8)  + 55  5
  540,  // 448 (10) + 55  6
  595,  // 512      + 55  7
  650,  // 576      + 55  8
  705,  // 640      + 55  9
  760,  // 704      + 55  10
  815,  // 768      + 55  11
  870,  // 832      + 55  12
  925,  // 896      + 55  13
  980,  // 960      + 55  14
};

int displayBrightnessLast = 0;
int ledBrightnessLast = 0;

int state = STATE_PRICE;

char ssid[64];
char password[64];
char passwordAP[64];
char currency[4];

bool connectSuccess = false;

// Preferences set by the mobile app, saved to EEPROM
byte pattern;
byte frequency;
byte red;
byte green;
byte blue;
byte ledBrightness;
byte displayBrightness;
byte autoBrightness;
byte alignment;
byte decimals;

//
// Initialization
//

void setup() {
  pinMode(CS, OUTPUT);
  pinMode(DIN, OUTPUT);
  pinMode(CLK, OUTPUT);
  pinMode(PHR, INPUT);
  pinMode(RST, OUTPUT);
  
  digitalWrite(RST, HIGH);

  unsigned long startMillis = millis();

  ss.shutdown(0, false);
  ss.clearDisplay(0);

  Wire.begin();

  // Count session for detecting hard resets
  byte session = eeprom_i2c_read(LOC_SESSION);
  session++;
  eeprom_i2c_write(LOC_SESSION, session);

  // Read preferences
  pattern = eeprom_i2c_read(LOC_PATTERN);
  frequency = eeprom_i2c_read(LOC_FREQUENCY);
  red = eeprom_i2c_read(LOC_RED);
  green = eeprom_i2c_read(LOC_GREEN);
  blue = eeprom_i2c_read(LOC_BLUE);
  ledBrightness = eeprom_i2c_read(LOC_LED_BRIGHTNESS);
  displayBrightness = eeprom_i2c_read(LOC_DISPLAY_BRIGHTNESS);
  autoBrightness = eeprom_i2c_read(LOC_AUTO_BRIGHTNESS);
  alignment = eeprom_i2c_read(LOC_ALIGNMENT);
  decimals = eeprom_i2c_read(LOC_DECIMALS);

  // Get the ball rolling
  saveIntensity();

  // Initialize hardware and software connections
  Serial.begin(9600);
  link.begin(9600);

  // Handle hard resets
  // You can apply a hard reset by unplugging and replugging the USB cable
  // three times while leaving power applied for less than one second each time.
  while (millis() - startMillis < 1000);
  if (session > 2) reset();
  eeprom_i2c_write(LOC_SESSION, 0);

  setInitialIntensity();

  // Display "bitbuddy"
  ss.setChar(0, 0, 'b', false);
  ss.setRow(0, 1, 0x10);
  ss.setRow(0, 2, 0x0f);
  ss.setChar(0, 3, 'b', false);
  ss.setRow(0, 4, 0x1c);
  ss.setChar(0, 5, 'd', false);
  ss.setChar(0, 6, 'd', false);
  ss.setRow(0, 7, 0x3b);

  sendAll();

  // Read credentials
  eeprom_i2c_read(LOC_CURRENCY, currency, 4);
  eeprom_i2c_read(LOC_SSID, ssid, 64);
  eeprom_i2c_read(LOC_PASSWORD, password, 64);
  eeprom_i2c_read(LOC_PASSWORD_AP, passwordAP, 64);

  // Initialize access point to set WiFi network and password from the mobile app
  WiFi.softAPConfig(ipAP, gateway, subnet);
  WiFi.softAP(name + " BitBuddy", passwordAP);
  delay(100);

  // Setup endpoints to access from the mobile app
  server.on("/status", onGetState);
  server.on("/authenticate", HTTP_POST, onSetCredentials);
  server.on("/validate", HTTP_POST, onValidate);
  server.on("/showPrice", HTTP_POST, onShowPrice);
  server.on("/showPassword", HTTP_POST, onShowPassword);
  server.on("/password", HTTP_POST, onSetPassword);
  server.on("/currency", HTTP_POST, onSetCurrency);
  server.on("/decimals", HTTP_POST, onSetDecimals);
  server.on("/frequency", HTTP_POST, onSetFrequency);
  server.on("/brightness", HTTP_POST, onSetBrightness);
  server.on("/autoBrightness", HTTP_POST, onSetAutoBrightness);
  server.on("/alignment", HTTP_POST, onSetAlignment);
  server.on("/pattern", HTTP_POST, onSetPattern);
  server.on("/color", HTTP_POST, onSetColor);
  server.onNotFound(handleNotFound);
  server.begin();

#if DEBUG
  delay(2000);
  Serial.println();
  Serial.println(getState());
  Serial.println("SSID: " + String(ssid));
  Serial.println("Password: " + String(password));
#endif

  performWifiConnect(ssid, password);
  onWifiConnected();

  Serial.println("Initialization complete");
  Serial.println(getState());
}

void loop() {
  server.handleClient();
  
  unsigned long currentMillis = millis();
  if (state == STATE_PRICE) {  
    if (WiFi.status() == WL_CONNECTED) {
      if (currentMillis - previousMillis >= frequency * 1000) {
        previousMillis = currentMillis;
        if (!isReady) {
          onWifiConnected();
        }
        fetchData();
      }
    } else {
      blinkNoData();
      if (currentMillis - previousMillis >= 100) {
        previousMillis = currentMillis;
        isReady = false;
      }
    }
  } else if (state == STATE_PASS) {
    blinkSetPass();
  }
  
  if (currentMillis - previousMillisIntensity >= 50) {
    previousMillisIntensity = currentMillis;
    setIntensity();
  }
}

void performWifiConnect(char ssid[], char password[]) {
  Serial.println("Attempting to connect to " + String(ssid) + "...");
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  WiFi.begin(ssid, password);
  WiFi.waitForConnectResult(CONNECTION_TIMEOUT);
}

void onWifiConnected() {
  Serial.println("WiFi connected " + WiFi.localIP().toString());
  isReady = true;
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  client.setInsecure();
}

void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Max-Age", "10000");
    server.sendHeader("Access-Control-Allow-Methods", "PUT,POST,GET,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.send(204);
  } else {
    server.send(404, "text/plain", "");
  }
}

//
// Webserver
//

// TODO: see if we can check for the existence of params and require them before doing anything
//       if there's a glitch and missing data is sent there could be unpredictable results
String getState() {
  String abString = autoBrightness > 0 ? "true" : "false";
  String dString = decimals > 0 ? "true" : "false";
  String json = "{";
  json += "\n  \"build\":" + String(BUILD) + ",";
  json += "\n  \"id\":\"" + id + "\",";
  json += "\n  \"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\n  \"name\":\"" + name + "\",";
  json += "\n  \"code\":\"" + code + "\",";
  json += "\n  \"currency\":\"" + String(currency) + "\",";
  json += "\n  \"frequency\":" + String(frequency) + ",";
  json += "\n  \"pattern\":\"" + patterns[(int)pattern - 1] + "\",";
  json += "\n  \"red\":" + String(red) + ",";
  json += "\n  \"green\":" + String(green) + ",";
  json += "\n  \"blue\":" + String(blue) + ",";
  json += "\n  \"ledBrightness\":" + String(ledBrightness) + ",";
  json += "\n  \"displayBrightness\":" + String(displayBrightness) + ",";
  json += "\n  \"autoBrightness\":" + abString + ",";
  json += "\n  \"alignment\":" + String(alignment) + ",";
  json += "\n  \"decimals\":" + dString + ",";
  json += "\n  \"color\":\"" + String(color) + "\"";
  json += "\n}";
  return json;
}

void onGetState() {
  String json = getState();
  Serial.println("Sending state: " + json);
  server.send(200, "application/json", json);
}

void onSetCredentials() {
  char ssidNew[64];
  char passNew[64];
  
  server.arg("ssid").toCharArray(ssidNew, 64);
  server.arg("password").toCharArray(passNew, 64);

  if (String(ssidNew) == String(ssid) && WiFi.status() == WL_CONNECTED) {
    Serial.println("Already connected to " + String(ssidNew));
    connectSuccess = true;
    server.send(200, "application/json", "{\"success\":true}");
    return;
  }

  server.send(200, "application/json", "{\"success\":true}");

  delay(1000);
  
  performWifiConnect(ssidNew, passNew);

  if (connectSuccess = WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to " + String(ssidNew) + " successfully!");
    strcpy(ssid, ssidNew);
    strcpy(password, passNew);
    eeprom_i2c_write(LOC_SSID, ssid, 64);
    eeprom_i2c_write(LOC_PASSWORD, password, 64);
  } else {
    Serial.println("Failed to connect to " + String(ssidNew) + ", attempting to reconnect to " + String(ssid));
    performWifiConnect(ssid, password);
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Could not return to old network!");
    }
  }
}

void onValidate() {
  Serial.println("onValidate " + String(connectSuccess > 0 ? "true" : "false"));
  if (connectSuccess) onGetState();
  else server.send(200, "application/json", "{\"success\": false}");
}

// TODO currency gets randomly reset after some period of time
void onSetCurrency() {
  server.arg("currency").toCharArray(currency, 4);
  Serial.println("Setting currency: " + String(currency));

  int httpCode = fetchData();
  if (httpCode == 200) {
    eeprom_i2c_write(LOC_CURRENCY, currency, 4);
    Serial.println("{\"currency\":\"" + String(currency) + "\"}");
    server.send(200, "application/json", "{\"currency\":\"" + String(currency) + "\"}");
  } else if (httpCode == 500) {
    Serial.println("Selected a currency with no price, setting back to USD");
    String defaultCurrency = "USD";
    defaultCurrency.toCharArray(currency, 4);
    eeprom_i2c_write(LOC_CURRENCY, currency, 4);
    server.send(500, "application/json", "{\"errorMessage\": \"No price\" }");
  } else {
    Serial.println("Unknown error fetching data with new currency" + String(currency));
    server.send(500, "application/json", "{\"errorMessage\": \"Unknown error\" }");
  }
}

void onSetDecimals() {
  decimals = server.arg("decimals") == "true" ? 1 : 0;
  eeprom_i2c_write(LOC_DECIMALS, decimals);
  Serial.println("Setting decimals: " + server.arg("decimals"));
  server.send(200, "application/json", "{\"decimals\":" + server.arg("decimals") + "}");
  printBaseAndExpo(lastBase, lastExpo);
}

void onSetFrequency() {
  int f = server.arg("frequency").toInt();
  if (f >= 2) {
    frequency = f;
    eeprom_i2c_write(LOC_FREQUENCY, frequency);
    Serial.println("Setting frequency: " + String(frequency));
    server.send(200, "application/json", "{\"frequency\":" + String(frequency) + "}");
  } else {
    String errorMessage = "Requested invalid frequency: " + String(f);
    Serial.println(errorMessage);
    server.send(500, "application/json", "{\"errorMessage\":\"" + errorMessage + "\"}");
  }
}

void onSetBrightness() {
  ledBrightness = server.arg("ledBrightness").toInt();
  displayBrightness = server.arg("displayBrightness").toInt();
  
  eeprom_i2c_write(LOC_LED_BRIGHTNESS, ledBrightness);
  eeprom_i2c_write(LOC_DISPLAY_BRIGHTNESS, displayBrightness);

  Serial.println("Setting led brightness: " + String(ledBrightness));
  Serial.println("Setting display brightness: " + String(displayBrightness));
  server.send(200, "application/json", "{\"ledBrightness\":" + String(ledBrightness) + ",\"displayBrightness\":" + String(displayBrightness) + "}");
}

void onSetAutoBrightness() {
  autoBrightness = server.arg("autoBrightness") == "true" ? 1 : 0;
  eeprom_i2c_write(LOC_AUTO_BRIGHTNESS, autoBrightness);
  Serial.println("Setting automatic brightness: " + server.arg("autoBrightness"));
  server.send(200, "application/json", "{\"autoBrightness\":" + server.arg("autoBrightness") + "}");
}

void onSetAlignment() {
  alignment = server.arg("alignment").toInt();
  eeprom_i2c_write(LOC_ALIGNMENT, alignment);
  Serial.println("Setting alignment: " + String(alignment));
  server.send(200, "application/json", "{\"alignment\":" + String(alignment) + "}");
  printBaseAndExpo(lastBase, lastExpo);
}

void onSetPattern() {
  String p = server.arg("pattern");
  if (p == patterns[0]) pattern = 1;
  else if (p == patterns[1]) pattern = 2;
  else {
    String errorMessage = "Requested invalid pattern: " + p;
    Serial.println(errorMessage);
    server.send(500, "application/json", "{\"errorMessage\":\"" + errorMessage + "\"}");
    return;
  }
  eeprom_i2c_write(LOC_PATTERN, pattern);
  
  sendPattern();

  Serial.println("Setting pattern: " + p);
  server.send(200, "application/json", "{\"pattern\":\"" + p + "\"}");
}

void onSetColor() {
  red = server.arg("r").toInt();
  green = server.arg("g").toInt();
  blue = server.arg("b").toInt();
  
  red = constrain(red, 0, 253);
  green = constrain(green, 0, 253);
  blue = constrain(blue, 0, 253);

  eeprom_i2c_write(LOC_RED, red);
  eeprom_i2c_write(LOC_GREEN, green);
  eeprom_i2c_write(LOC_BLUE, blue);
  
  sendColor();

  Serial.println("Setting color");
  server.send(200, "application/json", "{\"r\":" + String(red) + ",\"g\":" + String(green) + ",\"b\":" + String(blue) + "}");
}

void onShowPrice() {
  state = STATE_PRICE;
  Serial.println("Setting state STATE_PRICE");
  printBaseAndExpo(base, expo);
  server.send(200, "application/json", "{\"success\":true}");
}

void onShowPassword() {
  state = STATE_PASS;
  Serial.println("Setting state STATE_PASS");
  server.send(200, "application/json", "{\"success\":true}");
}

void onSetPassword() {
  char currentPassword[64];
  server.arg("currentPassword").toCharArray(currentPassword, 64);

  Serial.println("onSetPassword");

  if (String(passwordAP) == String(currentPassword)) {
    Serial.println("Current password matches, accepting new password...");

    server.arg("newPassword").toCharArray(passwordAP, 64);
    eeprom_i2c_write(LOC_PASSWORD_AP, passwordAP, 64);

    Serial.println("Updating AP password");
    server.send(200, "application/json", "{\"success\":true}");
  
    ss.setChar(0, 0, ' ', false);
    ss.setChar(0, 1, ' ', false);
    ss.setChar(0, 2, 'd', false);
    ss.setChar(0, 3, 'o', false);
    ss.setChar(0, 4, 'n', false);
    ss.setChar(0, 5, 'e', false);
    ss.setChar(0, 6, ' ', false);
    ss.setChar(0, 7, ' ', false);

    // Delay just in case app is connected to AP
    delay(5000);
  
    WiFi.softAP(name + " BitBuddy", passwordAP); 
    
    server.send(200, "application/json", "{\"success\":true}");

    state = STATE_PRICE;
  } else {
    Serial.println("Incorrect password");
    server.send(200, "application/json", "{\"success\":false}");
  }
}

//
// Prices
//

int fetchData() {
  http.begin(client, endpoint + "?currency=" + String(currency));
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println(payload);

    DynamicJsonDocument doc(1200);
    deserializeJson(doc, payload);

    base = doc["base"];
    expo = doc["expo"];
    
    printBaseAndExpo(base, expo);
  } else if (httpCode == 500) {
    String payload = http.getString();
    Serial.println(payload);

    DynamicJsonDocument doc(1200);
    deserializeJson(doc, payload);
    
    String error = doc["errorMessage"];
    if (error.startsWith("No price found")) {
      Serial.println("Price does not exist, resetting to USD");
      String defaultCurrency = "USD";
      defaultCurrency.toCharArray(currency, 4);
      eeprom_i2c_write(LOC_CURRENCY, currency, 4);
    } else {
      Serial.println("Uknown error: " + error);
    }
  }

  http.end();
  return httpCode;
}

//
// Brightness
//

int calcAverageIntensity() {
  int total = 0;
  for (int i = 0; i < INTENSITY_MAX; i++)
    total += intensity[i];

  return total / INTENSITY_MAX;
}

int upsampleIntensity(int raw) {
  int len = sizeof(steps) / sizeof(int);
  if (raw <= 0) return 0;
  if (raw >= len) return 1024;
  return steps[raw - 1]; 
}

int downsampleIntensity(int raw) {
  int len = sizeof(steps) / sizeof(int);
  for (int i = 0; i < len; i++) {
    if (raw < steps[i]) return i;
  }
  return len;
}

void saveIntensity() {
  // Save current brightness to history
  if (autoBrightness) {
    intensity[intensityIndex] = analogRead(PHR);
  } else {
    int brightness = (displayBrightness + ledBrightness) / 2;
    intensity[intensityIndex] = upsampleIntensity(brightness);
  }

  // Update position in history
  if (++intensityIndex >= INTENSITY_MAX) {
    intensityIndex = 0;
  }
}

void setInitialIntensity() {
  for (int i = 0; i < INTENSITY_MAX; i++) {
    saveIntensity();
  }

  int average = calcAverageIntensity();
  int downsampled = downsampleIntensity(average);

  sendBrightness(downsampled);
  ss.setIntensity(0, downsampled);
}

void setIntensity() {
  saveIntensity();

  if (autoBrightness) {
    // Heuristically downsample
    int average = calcAverageIntensity();
    int downsampled = downsampleIntensity(average);

    // Do not return to the previous value to prevent flickering
    if (downsampled == intensityLast) {
      downsampled = intensityCurrent;
    }

    // Update recent values
    if (downsampled != intensityCurrent) {
      intensityLast = intensityCurrent;
      intensityCurrent = downsampled;
    }

    // Calculate individual brightnesses based on custom balance
    int diff = abs(ledBrightness - displayBrightness);
    int half = diff / 2;
    int ledAverage = constrain(ledBrightness < displayBrightness ? downsampled - half : downsampled + half, 0, 15);
    int displayAverage = constrain(ledBrightness < displayBrightness ? downsampled + half : downsampled - half, 0, 15);

    // Update LED brightness if value has changed
    if (ledBrightnessLast != ledAverage) {
      sendBrightness(ledBrightnessLast = ledAverage);
    }

    // Update display brightness if value has changed
    if (displayBrightnessLast != displayAverage) {
      ss.setIntensity(0, displayBrightnessLast = displayAverage);
    }
  } else {
    // Update LED brightness if value has changed
    if (ledBrightness != ledBrightnessLast) {
      sendBrightness(ledBrightnessLast = ledBrightness);
    }

    // Update display brightness if value has changed
    if (displayBrightness != displayBrightnessLast) {
      ss.setIntensity(0, displayBrightnessLast = displayBrightness);
    }
  }
}

//
// Display
//

void printBaseAndExpo(int base, int expo) {
  ss.clearDisplay(0);

  lastBase = base;
  lastExpo = expo;

  if (!decimals) {
    base /= pow(10, expo);
    expo = 0;
  }

  unsigned long cur = 1;
  unsigned long max = 0;

  if (base > 0) {
    while (base >= cur) {
      cur *= 10;
      max++;
    }
    int startIndex = 0;
    if (alignment == ALIGN_RIGHT) startIndex = 8 - max;
    else if (alignment == ALIGN_CENTER_RIGHT) startIndex = int((8 - max) / 2) + (max % 2);
    else if (alignment == ALIGN_CENTER_LEFT) startIndex = (8 - max) / 2;
    for (int i = max - 1, cur = 10; i >= 0; i--) {
      ss.setDigit(0, startIndex + i, byte(base % 10), i < max - 1 && ((max - 1) - i) == expo);
      base /= 10;
    }
  } else {
    ss.setDigit(0, 0, (byte)0, false);
  }
}

void clearDisplay() {
  for (int i = 7; i >= 0; i--) {
    ss.setChar(0, i, ' ', false);
  }
}

void blinkNoData() {
  unsigned long currentMillis = millis();
  
  if (!isBlinking && currentMillis - previousMillisBlink >= 1000) {
    isBlinking = true;
    previousMillisBlink = currentMillis;
    ss.setChar(0, 0, ' ', false);
    ss.setChar(0, 1, 'n', false);
    ss.setChar(0, 2, 'o', false);
    ss.setChar(0, 3, ' ', false);
    ss.setChar(0, 4, 'd', false);
    ss.setChar(0, 5, 'a', false);
    ss.setRow(0, 6, 0x0f);
    ss.setChar(0, 7, 'a', false); 
  }

  if (isBlinking && currentMillis - previousMillisBlink >= 2000) {
    isBlinking = false;
    previousMillisBlink = currentMillis;
    clearDisplay();
  }
}

void blinkSetPass() {
  unsigned long currentMillis = millis();

  if (!isBlinking && currentMillis - previousMillisBlink >= 1000) {
    isBlinking = true;
    previousMillisBlink = currentMillis;
    ss.setChar(0, 0, '5', false);
    ss.setChar(0, 1, 'e', false);
    ss.setRow(0, 2, 0x0f);
    ss.setChar(0, 3, ' ', false);
    ss.setChar(0, 4, 'p', false);
    ss.setChar(0, 5, 'a', false);
    ss.setChar(0, 6, '5', false);
    ss.setChar(0, 7, '5', false); 
  }

  if (isBlinking && currentMillis - previousMillisBlink >= 2000) {
    isBlinking = false;
    previousMillisBlink = currentMillis;
    clearDisplay();
  }
}

//
// Serial
//

void printByteAsHex(byte b) {
  Serial.print("0x");
  if (b < 16) Serial.print('0');
  Serial.print(b, HEX);
  Serial.print(" ");
}

void writeCommand(byte cmd[], byte len) {
  unsigned long timeout = 100;
  int attempts = 5;

  while (true) {
    clearInputBuffer();
    
    link.write(0xFF);
    link.write(len);
    
    printByteAsHex(0xFF);
    printByteAsHex(len);
    
    for (int i = 0; i < len; i++) {
      link.write(cmd[i]);
      printByteAsHex(cmd[i]);
    }
    
    link.write(0xFE);
    printByteAsHex(0xFE);

    Serial.print("waiting " + String(timeout) + "ms... ");

    // Success
    if (waitForAck(timeout)) {
      Serial.println("success!");
      break;
    }

    // Restart slave
    if (attempts <= 0) {
      Serial.println("Failed all attempts, restarting slave...");
      resetSlave();
      delay(100);
      sendAll();
      break;
    }

    // Increase timeout and attempt to send again
    timeout *= 2;
    Serial.println("Write command failed, " + String(--attempts) + " attempts remaining");
  }

  delay(10);
}

void clearInputBuffer() {
  while (link.available() > 0) link.read();
}

bool waitForAck(unsigned long timeout) {
  unsigned long startMillis = millis();
  while (true) {
    // Response received
    if (link.available() > 0) {
      byte ack = link.read();
      return true;
    }

    // Timeout reached
    if (millis() - startMillis > timeout) {
      return false;
    }
  }
}

void resetSlave() {
  digitalWrite(RST, LOW);
  delay(10);
  digitalWrite(RST, HIGH);
}

void sendAll() {
  sendPattern();
  sendColor();
  if (autoBrightness == 0) {
    sendBrightness(ledBrightness);
  } else {
    // TODO send reading from PHR
  }
}

void sendPattern() {
  byte cmd[] = { 'p', pattern };
  Serial.print("sendPattern(" + String(pattern) + ") -> ");
  writeCommand(cmd, sizeof(cmd));
}

void sendColor() {
  byte cmd[] = { 'c', red, green, blue };
  Serial.print("sendColor(" + String(red) + ", " + String(green) + ", " + String(blue) + ") -> ");
  writeCommand(cmd, sizeof(cmd));
}

void sendBrightness(int brightness) {
  byte cmd[] = { 'b', byte(brightness + 1) };
  Serial.print("sendBrightness(" + String(brightness) + ") -> ");
  writeCommand(cmd, sizeof(cmd));
}

//
// EEPROM
//

void eeprom_i2c_write(byte from_addr, String str, int len) {
  char c[len];
  str.toCharArray(c, len);
  eeprom_i2c_write(from_addr, c, len);
}

void eeprom_i2c_write(byte from_addr, char data[], int len) {
  for (int i = 0; i < len; i++) {
    eeprom_i2c_write(from_addr + i, data[i]);
  }
}

void eeprom_i2c_write(byte from_addr, byte data) {
  Wire.beginTransmission(0x50);
  Wire.write(from_addr);
  Wire.write(data);
  Wire.endTransmission();
  delay(10);
}

byte eeprom_i2c_read(int from_addr) {
  read:
  Wire.beginTransmission(0x50);
  Wire.write(from_addr);
  Wire.endTransmission();

  /** @see https://arduino.stackexchange.com/questions/43007/why-is-a-delay1-necessary-before-wire-requestfrom */
  delay(1);

  Wire.requestFrom(0x50, 1);
  if (Wire.available()) return Wire.read();
  else {
    Serial.println("Could not read at position " + String(from_addr));
    goto read;
  }
}

// TODO terminate on NUL char?
void eeprom_i2c_read(int from_addr, char *out, int len) {
  for (int i = 0; i < len; i++) {
    out[i] = eeprom_i2c_read(from_addr + i);
    delay(10);
  }
}

//
// Reset
//

void reset() {
  char c[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
  };
  
  eeprom_i2c_write(LOC_CURRENCY, "USD", 4);
  eeprom_i2c_write(LOC_PATTERN, 1);
  eeprom_i2c_write(LOC_FREQUENCY, 5);
  eeprom_i2c_write(LOC_RED, 252);
  eeprom_i2c_write(LOC_GREEN, 252);
  eeprom_i2c_write(LOC_BLUE, 252);
  eeprom_i2c_write(LOC_LED_BRIGHTNESS, 1);
  eeprom_i2c_write(LOC_DISPLAY_BRIGHTNESS, 1);
  eeprom_i2c_write(LOC_AUTO_BRIGHTNESS, 1);
  eeprom_i2c_write(LOC_ALIGNMENT, 3);
  eeprom_i2c_write(LOC_DECIMALS, 0);
  eeprom_i2c_write(LOC_SESSION, 0);
  
  eeprom_i2c_write(LOC_SSID, c, 64);
  eeprom_i2c_write(LOC_PASSWORD, c, 64);
  eeprom_i2c_write(LOC_PASSWORD_AP, "12345678", 64);

  ESP.restart();
}
