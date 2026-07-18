#include "midi.h"
#include <Preferences.h>

// ==========================================
// MidiCircularBuffer Implementation
// ==========================================

MidiCircularBuffer::MidiCircularBuffer() 
    : head(0), tail(0), message_counter(0) {
}

void MidiCircularBuffer::push(const MidiMessage& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    
    buffer[head] = msg;
    buffer[head].message_number = message_counter++;
    
    size_t next_head = (head + 1) % MIDI_BUFFER_SIZE;
    if (next_head == tail) {
        // Buffer full, advance tail to drop oldest message
        tail = (tail + 1) % MIDI_BUFFER_SIZE;
    }
    head = next_head;
}

size_t MidiCircularBuffer::getMessages(MidiMessage* dest, size_t maxCount, uint32_t since_id, uint32_t& next_id) {
    std::lock_guard<std::mutex> lock(mtx);
    
    next_id = message_counter;
    size_t count = 0;
    size_t curr = tail;
    
    // If requesting the initial snapshot, start from the most recent maxCount messages
    if (since_id == 0xFFFFFFFF) {
        size_t total = (head >= tail) ? (head - tail) : (MIDI_BUFFER_SIZE - tail + head);
        if (total > maxCount) {
            if (head >= maxCount) {
                curr = head - maxCount;
            } else {
                curr = MIDI_BUFFER_SIZE - (maxCount - head);
            }
        }
    }
    
    while (curr != head && count < maxCount) {
        if (since_id == 0xFFFFFFFF || buffer[curr].message_number > since_id) {
            dest[count] = buffer[curr];
            count++;
        }
        curr = (curr + 1) % MIDI_BUFFER_SIZE;
    }
    
    return count;
}

void MidiCircularBuffer::clear() {
    std::lock_guard<std::mutex> lock(mtx);
    head = 0;
    tail = 0;
    message_counter = 0;
}

// ==========================================
// MidiParser Implementation
// ==========================================

MidiParser::MidiParser(MidiCircularBuffer& buffer)
    : logBuffer(buffer), enabledChannelsMask(0xFFFF), showClocks(false) {
    resetParserState();
    resetStats();
    loadSettings();
}

void MidiParser::loadSettings() {
    Preferences prefs;
    prefs.begin("midi-settings", true); // Open in read-only mode
    enabledChannelsMask = prefs.getUShort("channels", 0xFFFF);
    showClocks = prefs.getBool("show_clocks", false);
    prefs.end();
    Serial.printf("MIDI Parser settings loaded: channels mask = 0x%04X, show clocks = %s\n",
                  (uint16_t)enabledChannelsMask, showClocks ? "true" : "false");
}

void MidiParser::resetParserState() {
    runningStatus = 0;
    currentStatus = 0;
    dataCount = 0;
    expectedDataBytes = 0;
    inSysEx = false;
    sysexLength = 0;
}

void MidiParser::resetStats() {
    std::lock_guard<std::mutex> lock(statsMtx);
    clockCount = 0;
    lastClockUs = 0;
    clockHistoryIdx = 0;
    clockHistoryCount = 0;
    estimatedBpm = 0.0f;
    lastClockSystemMs = 0;
    activeSensingCount = 0;
    lastActiveSensingMs = 0;
    for (int i = 0; i < BPM_FILTER_SIZE; i++) {
        clockIntervalHistory[i] = 0;
    }
}

void MidiParser::handleRealTime(uint8_t b) {
    std::lock_guard<std::mutex> lock(statsMtx);
    
    if (b == 0xF8) { // Timing Clock
        clockCount++;
        uint32_t currentUs = micros();
        uint32_t nowMs = millis();
        
        if (lastClockUs > 0) {
            uint32_t diff = currentUs - lastClockUs;
            // Prevent crazy values from timer wraps or disruptions
            if (diff > 5000 && diff < 500000) { 
                clockIntervalHistory[clockHistoryIdx] = diff;
                clockHistoryIdx = (clockHistoryIdx + 1) % BPM_FILTER_SIZE;
                if (clockHistoryCount < BPM_FILTER_SIZE) {
                    clockHistoryCount++;
                }
                
                // Calculate moving average
                uint64_t sum = 0;
                for (uint8_t i = 0; i < clockHistoryCount; i++) {
                    sum += clockIntervalHistory[i];
                }
                float avgInterval = (float)sum / clockHistoryCount;
                
                // 24 clocks per quarter note
                // BPM = 60,000,000 us / (24 * avgInterval)
                estimatedBpm = 60000000.0f / (24.0f * avgInterval);
            }
        }
        lastClockUs = currentUs;
        lastClockSystemMs = nowMs;
        
        // Log Clock message only if showClocks is true
        if (showClocks) {
            uint8_t rawByte = 0xF8;
            statsMtx.unlock(); // Temporarily unlock to call emitMessage
            emitMessage("Clock", -1, &rawByte, 1);
            statsMtx.lock();
        }
        
    } else if (b == 0xFE) { // Active Sensing
        activeSensingCount++;
        lastActiveSensingMs = millis();
        // Discard from log to avoid filling it up
        
    } else { // Start (0xFA), Continue (0xFB), Stop (0xFC), System Reset (0xFF)
        const char* type = "Unknown Real-time";
        if (b == 0xFA) type = "Start";
        else if (b == 0xFB) type = "Continue";
        else if (b == 0xFC) type = "Stop";
        else if (b == 0xFF) type = "System Reset";
        
        statsMtx.unlock();
        emitMessage(type, -1, &b, 1);
        statsMtx.lock();
    }
}

void MidiParser::emitMessage(const char* type, int8_t channel, const uint8_t* data, uint8_t len) {
    MidiMessage msg;
    msg.message_number = 0; // Filled by buffer
    msg.received_us = micros();
    msg.channel = channel;
    msg.length = len < MIDI_MAX_RAW_BYTES ? len : MIDI_MAX_RAW_BYTES;
    memset(msg.raw, 0, MIDI_MAX_RAW_BYTES);
    memcpy(msg.raw, data, msg.length);
    strncpy(msg.type_str, type, sizeof(msg.type_str) - 1);
    msg.type_str[sizeof(msg.type_str) - 1] = '\0';
    
    logBuffer.push(msg);
}

void MidiParser::parseByte(uint8_t b) {
    // 1. Handle System Real-time messages immediately (can be interleaved anywhere)
    if (b >= 0xF8) {
        handleRealTime(b);
        return;
    }
    
    // 2. Handle Status bytes (MSB is set, except System Real-time which we already handled)
    if (b >= 0x80) {
        // End of SysEx if we see another status byte
        if (inSysEx) {
            if (b == 0xF7) {
                // Normal SysEx End
                if (dataCount < MIDI_MAX_RAW_BYTES) {
                    dataBuffer[dataCount++] = b;
                }
                sysexLength++;
                emitMessage("SysEx", -1, dataBuffer, dataCount);
            } else {
                // Interrupted SysEx
                emitMessage("SysEx (Interrupted)", -1, dataBuffer, dataCount);
            }
            inSysEx = false;
        }
        
        if (b == 0xF0) {
            // SysEx Start
            inSysEx = true;
            dataCount = 0;
            sysexLength = 0;
            if (dataCount < MIDI_MAX_RAW_BYTES) {
                dataBuffer[dataCount++] = b;
            }
            sysexLength++;
            expectedDataBytes = 0;
            currentStatus = b;
            runningStatus = 0; // SysEx clears running status
            
        } else if (b == 0xF7) {
            // EOX outside SysEx - ignore or emit as anomaly
            resetParserState();
            
        } else if (b >= 0xF1 && b <= 0xF6) {
            // System Common messages
            inSysEx = false;
            currentStatus = b;
            runningStatus = 0; // System common clears running status
            dataCount = 0;
            
            if (b == 0xF1) expectedDataBytes = 1;      // MTC Quarter Frame
            else if (b == 0xF2) expectedDataBytes = 2; // Song Position
            else if (b == 0xF3) expectedDataBytes = 1; // Song Select
            else if (b == 0xF6) expectedDataBytes = 0; // Tune Request
            else expectedDataBytes = 0;                // Undefined/Reserved
            
            if (expectedDataBytes == 0) {
                emitMessage(b == 0xF6 ? "Tune Request" : "System Common", -1, &b, 1);
                currentStatus = 0;
            }
            
        } else {
            // Channel Voice Messages (0x80 to 0xEF)
            inSysEx = false;
            currentStatus = b;
            runningStatus = b;
            dataCount = 0;
            
            uint8_t type = b & 0xF0;
            if (type == 0x80 || type == 0x90 || type == 0xA0 || type == 0xB0 || type == 0xE0) {
                expectedDataBytes = 2;
            } else if (type == 0xC0 || type == 0xD0) {
                expectedDataBytes = 1;
            } else {
                expectedDataBytes = 0; // Safety fallback
            }
        }
        return;
    }
    
    // 3. Handle Data bytes (MSB is clear: b < 0x80)
    if (inSysEx) {
        if (dataCount < MIDI_MAX_RAW_BYTES) {
            dataBuffer[dataCount++] = b;
        }
        sysexLength++;
        return;
    }
    
    // Use running status if current status is not active but running status is
    if (currentStatus == 0 && runningStatus >= 0x80) {
        currentStatus = runningStatus;
        dataCount = 0;
    }
    
    if (currentStatus >= 0x80) {
        if (dataCount < MIDI_MAX_RAW_BYTES) {
            dataBuffer[dataCount++] = b;
        }
        
        // Check if we got all expected data bytes
        if (dataCount >= expectedDataBytes) {
            // Package the full message (including status byte)
            uint8_t fullMessage[MIDI_MAX_RAW_BYTES + 1];
            fullMessage[0] = currentStatus;
            
            uint8_t copyLen = dataCount < (MIDI_MAX_RAW_BYTES - 1) ? dataCount : (MIDI_MAX_RAW_BYTES - 1);
            memcpy(fullMessage + 1, dataBuffer, copyLen);
            
            int8_t channel = (currentStatus & 0x0F);
            uint8_t type = currentStatus & 0xF0;
            const char* typeName = "Channel Msg";
            
            if (type == 0x80) typeName = "Note Off";
            else if (type == 0x90) {
                // If velocity is 0, it's practically a Note Off
                if (dataCount >= 2 && dataBuffer[1] == 0) {
                    typeName = "Note Off";
                } else {
                    typeName = "Note On";
                }
            }
            else if (type == 0xA0) typeName = "Poly Aftertouch";
            else if (type == 0xB0) typeName = "Control Change";
            else if (type == 0xC0) typeName = "Program Change";
            else if (type == 0xD0) typeName = "Channel Aftertouch";
            else if (type == 0xE0) typeName = "Pitch Bend";
            
            // Only log if the channel is enabled
            bool channelEnabled = true;
            if (channel >= 0 && channel < 16) {
                channelEnabled = ((enabledChannelsMask >> channel) & 1) == 1;
            }
            
            if (channelEnabled) {
                emitMessage(typeName, channel, fullMessage, copyLen + 1);
            }
            
            // Clear currentStatus but keep runningStatus for subsequent messages
            currentStatus = 0;
            dataCount = 0;
        }
    }
}

void MidiParser::getStats(uint32_t& out_clock_count, float& out_bpm, bool& out_clock_running, 
                          uint32_t& out_last_clock_ms, uint32_t& out_active_sensing_count,
                          uint32_t& out_last_active_sensing_ms, bool& out_active_sensing_present) {
    std::lock_guard<std::mutex> lock(statsMtx);
    
    uint32_t now = millis();
    
    // Check if MIDI clock is active
    if (lastClockSystemMs == 0 || (now - lastClockSystemMs) > CLOCK_OFFLINE_TIMEOUT_MS) {
        out_clock_running = false;
        out_bpm = 0.0f;
        out_last_clock_ms = (lastClockSystemMs == 0) ? 0 : (now - lastClockSystemMs);
    } else {
        out_clock_running = true;
        out_bpm = estimatedBpm;
        out_last_clock_ms = now - lastClockSystemMs;
    }
    
    out_clock_count = clockCount;
    out_active_sensing_count = activeSensingCount;
    out_last_active_sensing_ms = lastActiveSensingMs;
    
    // Check if Active Sensing is active (received within timeout)
    if (lastActiveSensingMs > 0 && (now - lastActiveSensingMs) < ACTIVE_SENSING_TIMEOUT_MS) {
        out_active_sensing_present = true;
    } else {
        out_active_sensing_present = false;
    }
}
