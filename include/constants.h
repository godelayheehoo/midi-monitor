#pragma once

// Wi-Fi Configuration
#define WIFI_CONNECTION_TIMEOUT_MS 10000

// Hotspot Mode credentials (if Router Mode fails or isn't configured)
#define WIFI_AP_SSID "MIDI-Monitor"
#define WIFI_AP_PASSWORD "12345678" // Minimum 8 chars for WPA2
#define WIFI_AP_CHANNEL 1

// Friendly Hostname for mDNS
#define MDNS_HOSTNAME "midi-monitor" // Accessible at http://midi-monitor.local

// HTTP Server Port
#define HTTP_PORT 80

// Buffer Configuration
#define MIDI_BUFFER_SIZE 1024        // Store last 1024 messages
#define MIDI_MAX_RAW_BYTES 8         // Max raw bytes per message (SysEx will be capped)

// Timing & Thresholds
#define CLOCK_OFFLINE_TIMEOUT_MS 1000  // Stop flashing/BPM display if clock stops for 1s
#define BPM_FILTER_SIZE 24             // Average timing clocks (24 clocks = 1 quarter note at 24 PPQN)
#define ACTIVE_SENSING_TIMEOUT_MS 1000 // Flag as disconnected if no active sensing within 1s

// Compilation modes
// Set to 1 to enable virtual MIDI generation for testing UI without hardware connected.
#define SIMULATE_MIDI 0
