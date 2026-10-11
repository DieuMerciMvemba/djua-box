#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
inline uint32_t testNow = 0;
inline int pinValues[64] = {};
inline int pinModes[64] = {};
inline uint32_t toneFrequency = 0;
inline uint32_t millis() { return testNow; }
inline void pinMode(uint8_t pin, int mode) { pinModes[pin] = mode; }
inline void digitalWrite(uint8_t pin, int value) {
    if (pinModes[pin] == OUTPUT) pinValues[pin] = value;
}
inline bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
inline uint32_t ledcWriteTone(uint8_t, uint32_t frequency) {
    toneFrequency = frequency;
    return frequency;
}
inline bool ledcWrite(uint8_t pin, uint32_t duty) {
    pinValues[pin] = duty ? HIGH : LOW;
    return true;
}
struct TestSerial {
    std::vector<std::string> logs;
    void println(const char* text) { logs.emplace_back(text); }
    void printf(const char* format, ...) {
        char line[256];
        va_list args;
        va_start(args, format);
        vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        logs.emplace_back(line);
    }
};
inline TestSerial Serial;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
