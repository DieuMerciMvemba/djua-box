#pragma once
#include "../../INDICATORS/host/Arduino.h"
inline void delay(unsigned long ms) { testNow += static_cast<uint32_t>(ms); }
