#include "web_server.h"
#include "index_html.h"
#include <WebServer.h>
#include <Preferences.h>

// Global web server instance on port 80
static WebServer server(HTTP_PORT);

// Static pointers to access backend components inside routing lambdas
static MidiParser *gParser = nullptr;
static MidiCircularBuffer *gBuffer = nullptr;

void setupWebServer(MidiParser &parser, MidiCircularBuffer &buffer) {
  gParser = &parser;
  gBuffer = &buffer;

  // Serve unified single page app (compressed in Flash)
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Content-Encoding", "gzip");
    // send_P sends from program memory (PROGMEM)
    server.send_P(200, "text/html", (const char *)INDEX_HTML_GZ,
                  INDEX_HTML_GZ_LEN);
  });

  // Serve clock and heartbeat statistics
  server.on("/api/status", HTTP_GET, []() {
    if (!gParser) {
      server.send(500, "application/json", "{\"error\":\"Not initialized\"}");
      return;
    }

    uint32_t clockCount = 0;
    float bpm = 0.0f;
    bool clockRunning = false;
    uint32_t lastClockMs = 0;
    uint32_t activeSensingCount = 0;
    uint32_t lastActiveSensingMs = 0;
    bool activeSensingPresent = false;

    gParser->getStats(clockCount, bpm, clockRunning, lastClockMs,
                      activeSensingCount, lastActiveSensingMs,
                      activeSensingPresent);

    uint32_t now = millis();
    uint32_t as_diff =
        (lastActiveSensingMs > 0) ? (now - lastActiveSensingMs) : 0;

    String json = "{";
    json += "\"clock_count\":" + String(clockCount) + ",";
    json +=
        "\"clock_running\":" + String(clockRunning ? "true" : "false") + ",";
    json += "\"estimated_bpm\":" + String(bpm, 2) + ",";
    json += "\"time_since_last_clock_ms\":" + String(lastClockMs) + ",";
    json += "\"active_sensing\":{";
    json += "\"count\":" + String(activeSensingCount) + ",";
    json += "\"time_since_last_ms\":" + String(as_diff) + ",";
    json += "\"is_present\":" + String(activeSensingPresent ? "true" : "false");
    json += "}";
    json += "}";

    server.send(200, "application/json", json);
  });

  // Serve new logs delta (polls since last processed ID)
  server.on("/api/logs", HTTP_GET, []() {
    if (!gBuffer) {
      server.send(500, "application/json", "{\"error\":\"Not initialized\"}");
      return;
    }

    uint32_t since_id = 0xFFFFFFFF;
    if (server.hasArg("since")) {
      since_id = server.arg("since").toInt();
    }

    uint32_t next_id = 0;
    // Dynamically allocate message fetch buffer to avoid ESP32 stack overflows
    MidiMessage *tempBuffer = new MidiMessage[100];
    if (!tempBuffer) {
      server.send(503, "application/json", "{\"error\":\"Out of memory\"}");
      return;
    }

    size_t count = gBuffer->getMessages(tempBuffer, 100, since_id, next_id);

    String json = "{";
    json += "\"next_id\":" + String(next_id) + ",";
    json += "\"messages\":[";
    for (size_t i = 0; i < count; i++) {
      if (i > 0)
        json += ",";
      json += "{";
      json +=
          "\"message_number\":" + String(tempBuffer[i].message_number) + ",";
      json += "\"message_type\":\"" + String(tempBuffer[i].type_str) + "\",";
      json += "\"channel\":" + String(tempBuffer[i].channel) + ",";
      json += "\"received_us\":" + String(tempBuffer[i].received_us) + ",";
      json += "\"raw\":[";
      for (uint8_t j = 0; j < tempBuffer[i].length; j++) {
        if (j > 0)
          json += ",";
        json += String(tempBuffer[i].raw[j]);
      }
      json += "]";
      json += "}";
    }
    json += "]";
    json += "}";

    delete[] tempBuffer;
    server.send(200, "application/json", json);
  });

  // Clear buffer and reset clock stats
  server.on("/api/clear", HTTP_POST, []() {
    if (!gBuffer || !gParser) {
      server.send(500, "application/json", "{\"error\":\"Not initialized\"}");
      return;
    }
    gBuffer->clear();
    gParser->resetStats();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // Export and download all messages currently in circular buffer
  server.on("/api/download", HTTP_GET, []() {
    if (!gBuffer || !gParser) {
      server.send(500, "application/json", "{\"error\":\"Not initialized\"}");
      return;
    }

    uint32_t clockCount = 0;
    float bpm = 0.0f;
    bool clockRunning = false;
    uint32_t lastClockMs = 0;
    uint32_t activeSensingCount = 0;
    uint32_t lastActiveSensingMs = 0;
    bool activeSensingPresent = false;

    gParser->getStats(clockCount, bpm, clockRunning, lastClockMs,
                      activeSensingCount, lastActiveSensingMs,
                      activeSensingPresent);

    uint32_t next_id = 0;
    MidiMessage *tempBuffer = new MidiMessage[MIDI_BUFFER_SIZE];
    if (!tempBuffer) {
      server.send(503, "application/json", "{\"error\":\"Out of memory\"}");
      return;
    }

    size_t count =
        gBuffer->getMessages(tempBuffer, MIDI_BUFFER_SIZE, 0xFFFFFFFF, next_id);

    // Signal file attachment download
    server.sendHeader("Content-Disposition",
                      "attachment; filename=midi_log.json");

    // Use chunked transfer encoding to send JSON segment-by-segment
    // prevents memory fragmentation and spikes
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    server.sendContent("{\n");
    server.sendContent("  \"metadata\": {\n");
    server.sendContent("    \"clock_count\": " + String(clockCount) + ",\n");
    server.sendContent("    \"estimated_bpm\": " + String(bpm, 2) + ",\n");
    server.sendContent(
        "    \"active_sensing_count\": " + String(activeSensingCount) + "\n");
    server.sendContent("  },\n");
    server.sendContent("  \"messages\": [\n");

    for (size_t i = 0; i < count; i++) {
      if (i > 0)
        server.sendContent(",\n");

      String chunk = "    {\n";
      chunk +=
          "      \"message_number\": " + String(tempBuffer[i].message_number) +
          ",\n";
      chunk += "      \"message_type\": \"" + String(tempBuffer[i].type_str) +
               "\",\n";
      chunk += "      \"channel\": " + String(tempBuffer[i].channel) + ",\n";
      chunk +=
          "      \"received_us\": " + String(tempBuffer[i].received_us) + ",\n";
      chunk += "      \"raw\": [";
      for (uint8_t j = 0; j < tempBuffer[i].length; j++) {
        if (j > 0)
          chunk += ", ";
        chunk += String(tempBuffer[i].raw[j]);
      }
      chunk += "]\n";
      chunk += "    }";

      server.sendContent(chunk);
    }

    server.sendContent("\n  ]\n");
    server.sendContent("}\n");
    server.sendContent(""); // Signals end of chunked response

    delete[] tempBuffer;
  });

  // Endpoint to save WiFi credentials from the dashboard portal
  server.on("/api/wifi", HTTP_POST, []() {
    if (!server.hasArg("ssid")) {
      server.send(400, "application/json", "{\"error\":\"Missing SSID\"}");
      return;
    }
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    // Save to Preferences (NVS Flash)
    Preferences prefs;
    prefs.begin("wifi-creds", false); // Open in read-write mode
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.end();

    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"WiFi credentials saved. Rebooting to connect...\"}");

    // Wait 1 second before rebooting so response is successfully dispatched
    delay(1000);
    ESP.restart();
  });

  server.begin();
  Serial.println("Web server listening on port 80");
}

void handleWebServerClient() { server.handleClient(); }
