# MIDI Monitor

## Overview

The MIDI Monitor is a simple diagnostic tool built around an ESP32. Its purpose is to observe and record all incoming DIN MIDI traffic, making it easier to diagnose unexpected MIDI behavior, verify device output, and inspect timing.

The device exposes a web interface over the local Wi-Fi network. All user interaction occurs through this web interface; the hardware itself has no display, buttons, or other user controls.

The initial implementation will prioritize simplicity and reliability, with the expectation that the web interface will grow more sophisticated over time.

---

## Hardware

* ESP32 (exact model TBD)
* DIN MIDI input over UART
* No display
* No buttons or other local user interaction

---

## Web Interface

The web interface should be responsive and work well on both phones and desktop browsers, with a slight preference for mobile usability.

### Status

Display the following information prominently at the top of the page:

* Total MIDI Clock message count
* Whether MIDI Clock appears to be running
* Estimated BPM (when enough clock messages have been received)
* Time since the last MIDI Clock message

### Message Log

Display a continuously updating log of all received MIDI messages.

By default, MIDI Clock messages should be hidden from the log (while still contributing to the clock statistics above). The interface should allow them to be shown later if desired.

Each log entry should include:

* 0-indexed message number
* Message type (Note On, Note Off, Control Change, Start, Stop, Continue, etc.)
* MIDI channel (`-1` for system/global messages)
* Relative receive time (time since boot or equivalent monotonic timestamp)
* Raw MIDI bytes

The log should automatically update as new messages arrive.

---

## Log Storage

The firmware should maintain a fixed-size circular buffer of recent MIDI messages.

When the buffer becomes full, the oldest messages should be overwritten.

Clock messages should contribute to the clock statistics regardless of whether they are shown in the default log view.

---

## Download

Provide a **Download Log** button that downloads the currently stored log as JSON.

Example structure:

```json
{
  "metadata": {
    "clock_count": 300,
    "estimated_bpm": 120.01
  },
  "messages": [
    {
      "message_number": 0,
      "message_type": "play",
      "channel": -1,
      "received_us": 1049234,
      "raw": [250]
    }
  ]
}
```

The schema may evolve over time as additional metadata is added.

---

## Clear Log

Provide a **Clear Log** button that clears all stored messages and resets the accumulated clock statistics.

---

## Future Expansion

The architecture should make it straightforward to add features such as:

* Message filtering
* Search
* Message type statistics
* Channel activity
* Live updates without page refresh
* Additional timing analysis
* Clock jitter visualization

These features are not required for the initial implementation.

---

## Agent Notes

* Place all configurable constants in `constants.h`.
* Place all pin assignments in `pins.h`.
* Keep the firmware focused on collecting, storing, and serving MIDI data. Presentation logic should live in the web interface whenever practical.
* Store timestamps using a monotonic clock (microseconds or milliseconds since boot) rather than wall-clock time. Any conversion to human-readable time should be performed by the web interface if needed.
* Design the API so the web interface consumes the same endpoints that external clients could use in the future.
