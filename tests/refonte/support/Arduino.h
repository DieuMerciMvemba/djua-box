#pragma once
#include "../../../TEST UNITAIRE/INDICATORS/host/Arduino.h"
inline void delay(unsigned long ms) { testNow += static_cast<uint32_t>(ms); }
