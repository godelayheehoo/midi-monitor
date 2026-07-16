#include "constants.h"
#include "midi.h"
#include "pins.h"
#include "web_server.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

// Instantiate MIDI Buffer and Parser
MidiCircularBuffer midiBuffer;
MidiParser midiParser(midiBuffer);

// FreeRTOS Task handle for the UART reader
TaskHandle_t midiReaderTaskHandle = NULL;

// MIDI UART reader task
void midiReaderTask(void *pvParameters) {
  MidiParser *parser = (MidiParser *)pvParameters;
  Serial.println("MIDI UART Reader Task started.");

  while (true) {
    // Read all available bytes in the UART buffer
    while (Serial2.available() > 0) {
      uint8_t b = Serial2.read();
      parser->parseByte(b);
    }
    // Small delay to yield CPU and prevent starving lower-priority tasks
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

#if SIMULATE_MIDI
// MIDI Simulation Task
void simulateMidiTask(void *pvParameters) {
  MidiParser *parser = (MidiParser *)pvParameters;
  Serial.println("MIDI Simulation Task started (SIMULATE_MIDI = 1).");

  uint32_t lastClockUs = micros();
  uint32_t clockPeriodUs = 20833; // ~120 BPM (24 clocks per beat, 500ms per
                                  // beat, 500/24 ms = 20.833ms = 20833us)
  uint32_t lastNoteMs = millis();
  uint32_t lastAsMs = millis();
  bool sendAs = true;
  uint32_t lastAsToggleMs = millis();
  uint8_t noteVal = 60; // Middle C
  bool noteOnState = false;

  while (true) {
    uint32_t nowUs = micros();
    uint32_t nowMs = millis();

    // 1. Generate MIDI Clock events at 120 BPM
    if (nowUs - lastClockUs >= clockPeriodUs) {
      parser->parseByte(0xF8);
      lastClockUs += clockPeriodUs;
    }

    // 2. Toggle sending active sensing every 10 seconds to test UI presence
    // detection
    if (nowMs - lastAsToggleMs >= 10000) {
      sendAs = !sendAs;
      lastAsToggleMs = nowMs;
      Serial.printf("Simulation: Active Sensing heartbeat sending %s\n",
                    sendAs ? "ON" : "OFF");
    }

    // 3. Generate Active Sensing heartbeat (0xFE) every 300ms if active
    if (sendAs && (nowMs - lastAsMs >= 300)) {
      parser->parseByte(0xFE);
      lastAsMs = nowMs;
    }

    // 4. Generate Note On and Note Off events every 2 seconds
    if (nowMs - lastNoteMs >= 2000) {
      if (!noteOnState) {
        // Emit Note On (Status: 0x90, Channel: 1, Data1: Note, Data2: Velocity)
        parser->parseByte(0x90);
        parser->parseByte(noteVal);
        parser->parseByte(100);
        noteOnState = true;
      } else {
        // Emit Note Off (Status: 0x80, Channel: 1, Data1: Note, Data2:
        // Velocity)
        parser->parseByte(0x80);
        parser->parseByte(noteVal);
        parser->parseByte(0);
        noteOnState = false;

        // Increment note value and wrap around
        noteVal++;
        if (noteVal > 72) {
          noteVal = 60;
        }
      }
      lastNoteMs = nowMs;
    }

    // Yield to other CPU threads
    delay(1);
  }
}
#endif

// Wi-Fi initialization function (Station with Access Point fallback)
void initWiFi() {
  String staSsid = WIFI_STA_SSID;

  if (staSsid.length() > 0) {
    Serial.printf("Attempting to connect to Wi-Fi Router: %s\n", WIFI_STA_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);

    uint32_t startAttemptTime = millis();
    // Wait for connection with a timeout
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttemptTime < WIFI_CONNECTION_TIMEOUT_MS) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Successfully joined Wi-Fi Router. IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    if (staSsid.length() > 0) {
      Serial.println("Failed to connect to Wi-Fi Router within timeout.");
    }
    Serial.println("Starting local hotspot (Access Point mode)...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL);
    Serial.print("Hotspot SSID: ");
    Serial.println(WIFI_AP_SSID);
    Serial.print("Access Dashboard IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

// Multicast DNS setup (midi-monitor.local resolution)
void initMDNS() {
  if (MDNS.begin(MDNS_HOSTNAME)) {
    Serial.printf("mDNS responder started. You can access the monitor at:\n");
    Serial.printf("===> http://%s.local\n", MDNS_HOSTNAME);
    MDNS.addService("http", "tcp", HTTP_PORT);
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
}

void setup() {
  // 1. Initialize Serial debugging port
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 MIDI Monitor Starting ===");

  // 2. Initialize Hardware UART2 for MIDI DIN input
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);
  Serial.printf("MIDI UART2 initialized on RX Pin GPIO %d, speed 31250 baud.\n",
                MIDI_RX_PIN);

  // 3. Connect to Wi-Fi router or fall back to hotspot
  initWiFi();

  // 4. Initialize local domain resolution (mDNS)
  initMDNS();

  // 5. Initialize the Web Server
  setupWebServer(midiParser, midiBuffer);

  // 6. Spawn the high-priority background task to monitor MIDI UART input
  xTaskCreatePinnedToCore(midiReaderTask, // Task function
                          "MidiReader",   // Name of task
                          4096,           // Stack size
                          &midiParser,    // Parameter passed to task
                          5,              // Priority (5 is high priority)
                          &midiReaderTaskHandle, // Task handle
                          1 // Core 1 (keeps core 0 free for Wi-Fi/IP stack)
  );

#if SIMULATE_MIDI
  // 7. Spawn the MIDI simulation task if enabled
  xTaskCreatePinnedToCore(simulateMidiTask, // Task function
                          "MidiSim",        // Name of task
                          4096,             // Stack size
                          &midiParser,      // Parameter passed to task
                          1,                // Priority (low priority)
                          NULL,             // Task handle
                          0 // Core 0 (shares core with Wi-Fi/IP stack)
  );
#endif
}

void loop() {
  // Poll the Web Server client to handle HTTP requests
  handleWebServerClient();

  // Tiny delay to prevent loop from taking 100% of Core 0 CPU
  delay(2);
}