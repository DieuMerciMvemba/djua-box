#pragma once
#include "d2_codec.h"
#include "../telemetry/telemetry_v3.h"
namespace djua {
constexpr size_t D3_PAYLOAD_BYTES = 75;
D2Error packD3(const TelemetryV3& frame, uint8_t output[D3_PAYLOAD_BYTES]);
D2Error encodeD3(const TelemetryV3& frame, const uint8_t* key, bool requireAuth,
                 D2Hmac hmac, char output[D2_SMS_CAPACITY]);
D2Error decodeD3(const char* sms, const uint8_t* key, bool requireAuth,
                 D2Hmac hmac, TelemetryV3& frame);
}
