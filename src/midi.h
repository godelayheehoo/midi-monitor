#pragma once

#include <Arduino.h>
#include <mutex>
#include <atomic>
#include "constants.h"

// MIDI message structure stored in circular buffer
struct MidiMessage {
    uint32_t message_number;
    uint32_t received_us;
    int8_t channel; // 0-15 (for channel 1-16), or -1 for system messages
    uint8_t length;
    uint8_t raw[MIDI_MAX_RAW_BYTES];
    char type_str[32];
};

// Thread-safe circular buffer for storing MIDI messages
class MidiCircularBuffer {
public:
    MidiCircularBuffer();
    void push(const MidiMessage& msg);
    // Retrieves up to maxCount messages with message_number > since_id.
    // Sets next_id to the ID of the next message that will be created.
    size_t getMessages(MidiMessage* dest, size_t maxCount, uint32_t since_id, uint32_t& next_id);
    void clear();

private:
    MidiMessage buffer[MIDI_BUFFER_SIZE];
    size_t head;
    size_t tail;
    uint32_t message_counter;
    std::mutex mtx;
};

// Custom MIDI state machine parser
class MidiParser {
public:
    MidiParser(MidiCircularBuffer& buffer);

    // Loads settings from NVS (Preferences)
    void loadSettings();

    // Parses a single byte from the MIDI stream.
    // Pushes completed messages to the circular buffer.
    void parseByte(uint8_t b);

    // Dynamic stats queries (thread-safe)
    void getStats(uint32_t& clock_count, float& bpm, bool& clock_running, 
                  uint32_t& last_clock_ms, uint32_t& active_sensing_count,
                  uint32_t& last_active_sensing_ms, bool& active_sensing_present);
                  
    void resetStats();

private:
    void emitMessage(const char* type, int8_t channel, const uint8_t* data, uint8_t len);
    void handleRealTime(uint8_t b);
    void resetParserState();

    MidiCircularBuffer& logBuffer;

    // Filter Settings (atomic for thread safety)
    std::atomic<uint16_t> enabledChannelsMask;
    std::atomic<bool> showClocks;

    // Parser State
    uint8_t runningStatus;
    uint8_t currentStatus;
    uint8_t dataBuffer[MIDI_MAX_RAW_BYTES];
    uint8_t dataCount;
    uint8_t expectedDataBytes;
    bool inSysEx;
    uint32_t sysexLength;

    // Clock and BPM Statistics
    std::mutex statsMtx;
    uint32_t clockCount;
    uint32_t lastClockUs;
    uint32_t clockIntervalHistory[BPM_FILTER_SIZE];
    uint8_t clockHistoryIdx;
    uint8_t clockHistoryCount;
    float estimatedBpm;
    uint32_t lastClockSystemMs;

    // Active Sensing Statistics
    uint32_t activeSensingCount;
    uint32_t lastActiveSensingMs;
};
