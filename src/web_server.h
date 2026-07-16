#pragma once

#include "midi.h"
#include <Arduino.h>

// Set up the HTTP web server and route handlers
void setupWebServer(MidiParser &parser, MidiCircularBuffer &buffer);

// Poll the web server client (should be called in loop() or a dedicated task)
void handleWebServerClient();
