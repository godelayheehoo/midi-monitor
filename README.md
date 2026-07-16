# MIDI Monitor for ESP32

A simple, robust MIDI diagnostics tool built for the ESP32. It listens to incoming DIN MIDI traffic, tracks statistics (BPM, active sensing, timing), and serves a premium, real-time web interface.

---

## How to Connect & Use

The MIDI Monitor runs its own web server. You interact with it by connecting to its Wi-Fi network and opening a web browser.

You can access the monitor at: **[http://midi-monitor.local](http://midi-monitor.local)**

---

### Step 1: Connect to the Wi-Fi

The device will attempt one of two ways to connect, depending on your setup:

#### Option A: Router Mode (Recommended)
If you configure the firmware with your local studio/home Wi-Fi credentials:
1. At boot, the ESP32 will automatically connect to your Wi-Fi router.
2. Connect your laptop, tablet, or phone to that same Wi-Fi router.
3. Open a browser and go to **[http://midi-monitor.local](http://midi-monitor.local)**.

#### Option B: Hotspot Mode (Fallback / Offline)
If the ESP32 cannot find your configured Wi-Fi network, or if you did not configure any credentials:
1. The ESP32 will broadcast its own local Wi-Fi hotspot.
2. Open your computer or phone's Wi-Fi Settings and look for:
   * **Network Name (SSID)**: `MIDI-Monitor`
   * **Password**: `12345678`
3. Connect your device to this Wi-Fi network.
4. Open a browser and go to **[http://midi-monitor.local](http://midi-monitor.local)**.
   * *Note: If your device does not support `.local` addresses (some mobile phones), you can navigate directly to the IP address: [http://192.168.4.1](http://192.168.4.1)*

---

### Step 2: Using the Web Dashboard

Once loaded, the dashboard provides a real-time monitor:

* **Clock Status Panel**: Displays total MIDI Clock messages, current estimated BPM (smoothed via moving average), time elapsed since the last clock, and whether the clock is active.
* **BPM Beat Light**: A circular pulse indicator that beats in perfect time with the incoming MIDI Clock tempo.
* **Active Sensing Monitor**: Displays a badge indicating whether Active Sensing (`0xFE` heartbeat) is currently being sent by your controller (updates in real-time).
* **Live Message Log**: Lists all incoming MIDI messages. You can toggle whether to show timing clock events in the log.
* **Controls**:
  * **Clear Log**: Wipes the message buffer and resets timing/clock stats.
  * **Download Log**: Saves the current log of messages in a JSON format matching the schema for further timing analysis.

---

## Developer / Compilation Setup

This project is built using [PlatformIO](https://platformio.org/).

### Configuring Wi-Fi Credentials

The project supports two ways to configure Router Mode credentials:

1. **Option A: Static Headers (Recommended for Dev)**
   - Create a file named `include/credentials.h` (which is ignored by git via `.gitignore`).
   - Define your credentials in it using the following format:
     ```cpp
     #pragma once
     #define WIFI_STA_SSID "Your_WiFi_Name"
     #define WIFI_STA_PASSWORD "Your_WiFi_Password"
     ```

2. **Option B: Fallback Configuration Portal (Recommended for Deployment)**
   - If `include/credentials.h` is not present, or if `WIFI_STA_SSID` is left empty (`""`), the device will boot up.
   - If it has saved Wi-Fi credentials in its internal non-volatile flash storage (Preferences), it will connect using those.
   - If not, it will boot directly into **Hotspot Mode** (AP mode, SSID: `MIDI-Monitor`, password `12345678`).
   - Connect your device to the hotspot, go to [http://midi-monitor.local](http://midi-monitor.local), and enter your Router SSID and Password in the **Wi-Fi Settings** panel.
   - The device will save these credentials to non-volatile storage and reboot to connect to your router.

### Simulation Mode (No Hardware Needed)
To test the web interface, API, or build without being connected to a physical MIDI input circuit:
1. Open [include/constants.h](file:///Users/james/projects/midi_monitor/include/constants.h)
2. Set `#define SIMULATE_MIDI 1`
3. Compile and upload. The ESP32 will simulate clock signals at 120 BPM and periodic MIDI notes.
