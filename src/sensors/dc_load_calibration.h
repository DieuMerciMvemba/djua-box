#pragma once
#include <stdint.h>
#include <math.h>

struct DcLoadCalibration {
    double shuntOhms = 0, currentLsbA = 0;
    uint16_t reg = 0;
};
// TI INA219: Cal = trunc(0.04096 / (Current_LSB * Rshunt)).
inline bool makeDcLoadCalibration(double ratedA, double ratedMv, DcLoadCalibration& out) {
    out = DcLoadCalibration{};
    if (!isfinite(ratedA) || !isfinite(ratedMv) || ratedA <= 0 || ratedA > 100 ||
        ratedMv <= 0 || ratedMv >= 320) return false;
    const double resistance = ratedMv / 1000.0 / ratedA;
    // 30000 comptes au nominal laissent une marge avant saturation signed16.
    double lsb = ratedA / 30000.0;
    const double minimumLsb = 0.04096 / (65534.0 * resistance);
    if (lsb < minimumLsb) lsb = minimumLsb;
    const double candidate = floor(0.04096 / (lsb * resistance));
    if (!isfinite(candidate) || candidate < 2 || candidate > 65535) return false;
    // Bit0 du registre de calibration reserve, toujours zero.
    out.reg = static_cast<uint16_t>(static_cast<uint32_t>(candidate) & ~1U);
    out.shuntOhms = resistance;
    out.currentLsbA = 0.04096 / (out.reg * resistance);
    return true;
}
