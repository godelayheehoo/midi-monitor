# ESP32 MIDI Monitor — Technical Implementation Plan

This document outlines the fine technical details for building the ESP32-based MIDI Monitor, based on the requirements in [brief-overview.md](file:///Users/james/Documents/PlatformIO/Projects/midi-monitor/brief-overview.md).

---

## 1. Wi-Fi & Friendly Domain Configuration (`midi-monitor.local`)

We will configure the ESP32 to support two Wi-Fi modes so you can always connect to it:

1. **Router Mode (Station / STA)**: The ESP32 joins your existing home or studio Wi-Fi network. This is the most convenient mode because your computer/phone stays connected to your normal network (and the internet) while accessing the MIDI Monitor.
2. **Hotspot Mode (Access Point / AP)**: The ESP32 broadcasts its own Wi-Fi network (like a wireless hotspot) called `MIDI-Monitor`. You connect your phone or computer directly to this network. Use this if you are using the device away from your home router.

**Our Automatic Strategy**: 
The ESP32 will first try to connect to your existing Wi-Fi network. If it fails to connect within 10 seconds (or if no credentials are configured), it will automatically fall back and start its own **Hotspot** network so you can still connect to it.

### Friendly Local Address (mDNS)
To avoid needing to find the ESP32's numeric IP address, we will run the Multicast DNS (mDNS) responder library (`ESPmDNS.h`).
* Both in Router Mode and Hotspot Mode, the ESP32 will register the hostname `midi-monitor`.
* You can access the interface in your web browser by navigating to: **`http://midi-monitor.local`**
* In Hotspot Mode, we will also ensure it responds on the standard default gateway IP `http://192.168.4.1` for client devices that do not support mDNS resolution (such as some mobile operating systems when connected to a local Wi-Fi network without cellular access).

---

## 2. Hardware & Pins Configuration (`pins.h`)

Since the hardware has no display or physical controls, the ESP32's primary hardware interaction is receiving MIDI signals via UART.

We will configure:
* **UART Port**: UART2 (highly recommended for ESP32 as UART0 is reserved for USB flashing/monitoring).
* **RX Pin**: GPIO 16 (default RX2 pin on many ESP32 boards).
* **TX Pin**: GPIO 17 (default TX2, not strictly needed for input-only but initialized for completeness).
* **MIDI Baud Rate**: `31250` (standard MIDI speed).

```cpp
// pins.h
#pragma once

#define MIDI_UART_NUM 2
#define MIDI_RX_PIN 16
#define MIDI_TX_PIN 17
```

---

## 3. Firmware Constants (`constants.h`)

All configurable parameters will be stored in `constants.h` to make it easy to tune buffer sizes, Wi-Fi networks, and timing thresholds.

```cpp
// constants.h
#pragma once

// Wi-Fi Configuration
#define WIFI_STA_SSID ""         // Enter your studio Wi-Fi name here to use Router Mode
#define WIFI_STA_PASSWORD ""     // Enter your studio Wi-Fi password here
#define WIFI_CONNECTION_TIMEOUT_MS 10000

#define WIFI_AP_SSID "MIDI-Monitor"
#define WIFI_AP_PASSWORD "12345678" // Minimum 8 chars for WPA2
#define WIFI_AP_CHANNEL 1

// Friendly Hostname for mDNS
#define MDNS_HOSTNAME "midi-monitor" // Resolves to http://midi-monitor.local

// HTTP Port
#define HTTP_PORT 80

// Buffer Configuration
#define MIDI_BUFFER_SIZE 1024    // Increased buffer size to store more message history
#define MIDI_MAX_RAW_BYTES 8     // Max bytes logged per message (protects against heap fragmentation)

// Timing & Filtering
#define CLOCK_OFFLINE_TIMEOUT_MS 1000  // Clear BPM if no clock messages within this duration
#define BPM_FILTER_SIZE 24             // Number of MIDI clock periods to average for BPM estimation
#define ACTIVE_SENSING_TIMEOUT_MS 1000 // Time after which active sensing is considered disconnected
```

---

## 4. MIDI Parsing Engine (`midi.h` & `midi.cpp`)

To parse incoming MIDI bytes reliably, we will write a custom state machine that handles:
1. **Running Status**: If a status byte is omitted, the last status byte is reused.
2. **Real-time Messages (0xF8 - 0xFF)**: Interleaved at any point in the stream without disrupting the status of standard messages.
3. **Active Sensing Heartbeat (`0xFE`)**: 
   * Active Sensing is a heartbeat byte sent every 300ms by some MIDI controllers.
   * If stored in the circular buffer, it would quickly overflow it.
   * **Instead of logging `0xFE` in the buffer, we will track it in global status variables**:
     * `active_sensing_count`: Total number of Active Sensing messages received.
     * `last_active_sensing_time`: Timestamp of the last received Active Sensing message.
     * `active_sensing_present`: A boolean flag calculated as `(current_time - last_active_sensing_time) < ACTIVE_SENSING_TIMEOUT_MS`.
   * This is sent to the Web UI via the status endpoint and displayed as a status badge.

### Message Structure

```cpp
struct MidiMessage {
    uint32_t message_number;
    uint32_t received_us; // Monotonic microsecond timestamp
    int8_t channel;       // 0-15, or -1 for System messages
    uint8_t length;       // Actual raw bytes length
    uint8_t raw[MIDI_MAX_RAW_BYTES];
    const char* type_str;
};
```

---

## 5. Log Storage & Thread Safety

The Web Server runs on a separate FreeRTOS task or core than the main execution loop or UART interrupts. To prevent race conditions:
* We will implement a thread-safe circular buffer.
* A standard C++ lock/semaphore or the FreeRTOS `portMUX_TYPE` (critical section) will guard read/write indices.

```cpp
class MidiCircularBuffer {
public:
    void push(const MidiMessage& msg);
    size_t getMessages(MidiMessage* dest, size_t maxCount, uint32_t since_id, uint32_t& next_id);
    void clear();
private:
    MidiMessage buffer[MIDI_BUFFER_SIZE];
    size_t head = 0;
    size_t tail = 0;
    uint32_t message_counter = 0;
    portMUX_TYPE mux = SPINLOCK_INITIALIZER;
};
```

---

## 6. Web Server and REST API Design

We will use the standard `WebServer.h` library along with `ESPmDNS.h` for mDNS discovery.

### HTTP Endpoints

| Endpoint | Method | Response Format | Description |
|---|---|---|---|
| `/` | `GET` | HTML / CSS / JS | Main Single Page Application, compressed using gzip and stored in Flash. |
| `/api/status` | `GET` | JSON | Returns current clock count, BPM, clock status, time since last clock, and **Active Sensing status** (`count`, `last_received_ms`, `is_present`). |
| `/api/logs` | `GET` | JSON | Returns message logs. Supports query param `since` (returns only messages with `message_number > since` for delta updates). |
| `/api/clear` | `POST` | JSON | Resets the circular buffer and clock/sensing statistics. |
| `/api/download` | `GET` | JSON (as download attachment) | Downloads the entire contents of the buffer as a JSON file. |

---

## 7. Web Interface Aesthetics & UX

We will design a premium, dark-mode web application embedded directly in the ESP32. To keep the codebase clean and avoid complex multi-file SPIFFS builds, we will write a single unified file (HTML with inline CSS and JS) and gzip it.

### Design Elements:
* **Palette**: Dark slate gray background (`#0f172a`), neon blue accents (`#0ea5e9`), neon green status indicators (`#10b981`), and subtle border colors.
* **Typography**: Outfit or Inter via Google Fonts with system sans-serif fallback.
* **Real-time BPM Pulse**: A circular pulse indicator that expands and fades in rhythm with the measured BPM (calculated in JS using `estimated_bpm`).
* **Active Sensing Badge**: An indicator showing either "Active Sensing: Active (Count: 482)" in green, or "Active Sensing: Absent" in grey/muted tone.
* **Efficient Updates**: The Javascript application polls `/api/logs?since=X` every 200–500ms to fetch only new messages, appending them to the DOM instead of re-rendering the whole table.

---

## 8. Verification & Simulation Plan

Because access to physical MIDI hardware can be limited during initial testing, we will write a **Simulation Mode** compile flag (`SIMULATE_MIDI`) in `constants.h`.
* If enabled, a virtual MIDI generator task will run in the background, spawning synthetic MIDI clocks at 120 BPM, along with periodic Note On / Note Off messages, and optional Active Sensing messages.
* This allows full verification of:
  1. The circular buffer thread-safety.
  2. The BPM estimation algorithm.
  3. The API endpoint serialization.
  4. The web interface live updates, filtering, active sensing detection, and download features.
