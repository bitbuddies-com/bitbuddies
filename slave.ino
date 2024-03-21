/**
 * Run this code on the ATtiny85 to control LEDs
 * @author Danny Buonocore, BitBuddies LLC
 */

#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include "hsv.h"

#define RX  3
#define TX  4
#define LED 1

#define PATTERN_SOLID   1
#define PATTERN_FADE    2
#define PATTERN_RAINBOW 3

#define NUM_PATTERNS  3
#define MAXHUE        256 * 6

#define NUM_LEDS 24

SoftwareSerial link(RX, TX);

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_LEDS, LED, NEO_GRB + NEO_KHZ800);

int pattern = PATTERN_SOLID;
byte brightness = 5;
byte red = 0;
byte green = 0;
byte blue = 0;

unsigned long previousMillis = 0;
int t = 0;
int hue = 0;

bool receivedData = false;

void setup() {
  pinMode(LED, OUTPUT);
  link.begin(9600);
  pixels.begin();
} 

void loop() {
  if (link.available() >= 2) {
    if (link.read() == 0xFF) {
      byte len = link.read();
      while (link.available() < len + 1);
      
      byte cmd[len];
      for (int i = 0; i < len; i++) {
        cmd[i] = link.read();
        if (cmd[i] == 0xFE) {
          return;
        }
      }

      // Process command
      if (link.read() == 0xFE) {
        receivedData = true;

        // Acknowledge the command has been received
        link.write(0x06);

        // Brightness
        if (cmd[0] == 'b') {
          int b = cmd[1];
          if (b > 0 && b <= 16) {
            brightness = b;
          }
        }

        // Color
        if (cmd[0] == 'c') {
          red = cmd[1] == 1 ? 0 : cmd[1];
          green = cmd[2] == 1 ? 0 : cmd[2];
          blue = cmd[3] == 1 ? 0 : cmd[3];
        }

        // Pattern
        if (cmd[0] == 'p') {
          byte p = cmd[1];
          if (p > 0 && p <= NUM_PATTERNS) {
            pattern = p;
          }
        }
      }
    }
  }

  if (receivedData) {
    unsigned long maxMillis = 40;
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= maxMillis) {
      previousMillis = currentMillis;
      updatePixels();
    }
  }
}

void updatePixels() {
  float multiplier = brightness / 16.0;

  for (int i = 0; i < NUM_LEDS; i++) {
    if (pattern == PATTERN_RAINBOW) {
      pixels.setPixelColor(
        (i + t) % NUM_LEDS,
        getPixelColorHsv(i, i * (MAXHUE / NUM_LEDS), 100, int(255 * multiplier))
      );
    } else if (pattern == PATTERN_FADE) {
      pixels.setPixelColor(
        (i + t) % NUM_LEDS,
        getPixelColorHsv(i, hue, 255, int(255 * multiplier))
      );
    } else {
      int r = red * multiplier;
      int g = green * multiplier;
      int b = blue * multiplier;
      pixels.setPixelColor(i, r, g, b);
    }
  }

  pixels.show();

  if (pattern != PATTERN_SOLID) {
    hue += 2;
    hue %= MAXHUE;
  }

  t++;
  t %= NUM_LEDS;
}

int clamp(int i) {
  if (i < 0) return NUM_LEDS - i;
  if (i > NUM_LEDS - 1) return i - NUM_LEDS;
  return i;
}
