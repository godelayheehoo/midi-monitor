#pragma once

#include <Arduino.h>

// Handle different ESP32 chip architectures (e.g. S2/S3/C3 vs Standard ESP32)
#if defined(CONFIG_IDF_TARGET_ESP32)
  // Standard ESP32 has 3 hardware UARTs (0, 1, 2). We use UART2 (Serial2).
  #define MIDI_SERIAL Serial2
  #define MIDI_UART_NUM 2
  #define MIDI_RX_PIN 16
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
  // ESP32-S2 has 2 hardware UARTs (0, 1). We use UART1 (Serial1).
  #define MIDI_SERIAL Serial1
  #define MIDI_UART_NUM 1
  // On Lolin S2 Mini, pin 16 is exposed and free to use.
  #define MIDI_RX_PIN 16
#else
  // Fallback for other variants (e.g., ESP32-S3 or ESP32-C3)
  #define MIDI_SERIAL Serial1
  #define MIDI_UART_NUM 1
  #define MIDI_RX_PIN 16
#endif
