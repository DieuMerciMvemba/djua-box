#pragma once
#include <stdint.h>
#include <math.h>
namespace djua {
enum class LocationSource : uint8_t { None, NeoGnss, Cellular };
struct LocationFix {
    double latitude = 0, longitude = 0;
    bool valid = false;
    LocationSource source = LocationSource::NeoGnss;
    uint32_t ageMs = 0;
    double accuracyM = 0, hdop = 0;
    bool accuracyKnown = false, hdopKnown = false;
};
inline bool locationUsable(const LocationFix& fix, uint32_t maxAge) {
    return fix.valid && fix.source != LocationSource::None && fix.ageMs <= maxAge &&
        isfinite(fix.latitude) && isfinite(fix.longitude) &&
        fabs(fix.latitude) <= 90 && fabs(fix.longitude) <= 180 &&
        (!fix.accuracyKnown || (isfinite(fix.accuracyM) && fix.accuracyM >= 0));
}
class LocationProvider {
public:
    virtual ~LocationProvider() = default;
    virtual void update() = 0;
    virtual LocationFix read() = 0;
};
class LocationService {
public:
    explicit LocationService(LocationProvider& primary, LocationProvider* cell = nullptr)
        : primary_(primary), cell_(cell) {}
    void update() { primary_.update(); if (cell_) cell_->update(); }
    LocationFix read(uint32_t maxAge) {
        LocationFix fix = primary_.read();
        if (fix.source == LocationSource::NeoGnss && locationUsable(fix, maxAge)) return fix;
        if (cell_) { fix = cell_->read();
            if (fix.source == LocationSource::Cellular && locationUsable(fix, maxAge)) return fix; }
        fix.valid = false; return fix;
    }
private:
    LocationProvider& primary_;
    LocationProvider* cell_;
};
inline bool precisionGeofenceUsable(const LocationFix& fix, uint32_t maxAge, double maxAccuracy) {
    // Unknown accuracy is exposed as unknown; never convert HDOP into invented metres.
    return fix.source == LocationSource::NeoGnss && locationUsable(fix, maxAge) &&
        (!fix.accuracyKnown || fix.accuracyM <= maxAccuracy);
}
}
