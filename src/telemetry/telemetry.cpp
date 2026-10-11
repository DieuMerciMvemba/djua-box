#include "telemetry.h"
#include <math.h>

TelemetryData buildV3Telemetry(const djua::TelemetryV3& frame, const RTCData& rtc) {
    TelemetryData data{}; data.v3Present = true; data.v3 = frame;
    data.intervalSeconds = frame.interval;
    data.timestampValid = formatRTCTimestamp(rtc, data.timestamp, sizeof(data.timestamp));
    return data;
}

// =====================================================
// CONSTRUCTION DE LA TELEMETRIE
// =====================================================

TelemetryData buildTelemetry(
    const PowerData& battery,
    const SolarPowerData& solar,
    const ACPowerData& acLoad,
    const GPSData& gps,
    const RTCData& rtc,
    unsigned long intervalSeconds,
    const DcLoadPowerData& dcLoad
)
{
    TelemetryData data = {};

    // =================================================
    // HORODATAGE REEL DS1302
    // =================================================

    data.timestampValid = formatRTCTimestamp(
        rtc,
        data.timestamp,
        sizeof(data.timestamp)
    );

    if (!data.timestampValid)
    {
        data.timestamp[0] = '\0';
    }

    // =================================================
    // GPS REEL
    // =================================================

    if (gps.valid)
    {
        data.latitude = gps.latitude;
        data.longitude = gps.longitude;
    }
    else
    {
        /*
         * Pas de faux emplacement Kinshasa.
         *
         * Le backend actuel attend latitude/longitude,
         * donc 0.0 indique simplement qu'aucune
         * position GPS n'est disponible.
         */
        data.latitude = 0.0;
        data.longitude = 0.0;
    }

    // =================================================
    // BATTERIE REELLE - INA219
    // =================================================

    data.batteryMeasurementValid = battery.valid;
    if (battery.valid)
    {
        data.batteryVoltage = battery.voltage;
        data.batteryCurrent = battery.current;
        data.batteryPower = battery.power;
    }

    // SOLAIRE

    data.solarMeasurementValid = solar.valid;
    // Une absence de mesure ne doit jamais devenir une production nulle.
    data.solarVoltage = data.solarCurrent = data.solarPower =
        data.solarEnergyIntervalWh = NAN;
    if (solar.valid)
    {
        data.solarVoltage = solar.voltage;
        data.solarCurrent = solar.current;
        data.solarPower = solar.power;
        const float intervalHours = intervalSeconds / 3600.0F;
        data.solarEnergyIntervalWh = solar.power * intervalHours;
    }

    // AC LOAD. Une mesure invalide n'est jamais presentee comme une mesure
    // reelle : les grandeurs sont mises a zero et le drapeau reste faux.
    data.acMeasurementValid = acLoad.valid;
    if (acLoad.valid)
    {
        data.acLoadVoltage = acLoad.voltageRms;
        data.acLoadCurrent = acLoad.currentRms;
        data.acActivePower = acLoad.activePower;
        data.acApparentPower = acLoad.apparentPower;
        data.acPowerFactor = acLoad.powerFactor;

        const float intervalHours = intervalSeconds / 3600.0F;
        data.acEnergyIntervalWh = acLoad.activePower * intervalHours;
        data.acEnergyIntervalVAh = acLoad.apparentPower * intervalHours;
    }

    data.dcLoadMeasurementValid = dcLoad.valid;
    data.dcLoadVoltage = data.dcLoadCurrent = data.dcLoadPower = data.dcLoadEnergyIntervalWh = NAN;
    if (dcLoad.valid) {
        data.dcLoadVoltage = dcLoad.voltage;
        data.dcLoadCurrent = dcLoad.current;
        data.dcLoadPower = dcLoad.power;
    }
    // L'energie DC est ajoutee par la fenetre temporelle, jamais extrapolee
    // depuis la derniere puissance.

    // INTERVALLE

    data.intervalSeconds = intervalSeconds;

    return data;
}
