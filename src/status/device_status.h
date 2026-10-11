#ifndef DJUA_DEVICE_STATUS_H
#define DJUA_DEVICE_STATUS_H
void recordV2SensorStatus(const bool available[6], const bool valid[6], bool acValid,
                          bool rtcValid, float batteryVoltage);

// Couche de decision. Les mesures et les transports restent dans leurs modules.
void initDeviceStatus(bool powerAvailable, bool rtcValid);
// En acquisition groupee, differer l'alerte jusqu'au recordSensorStatus suivant.
void recordSolarStatus(bool solarAvailable, bool solarValid, bool deferAlertRefresh = false);
void recordDcLoadStatus(bool available, bool valid, bool deferAlertRefresh = false);
void recordSensorStatus(bool powerAvailable, bool powerValid, bool acValid,
                        bool rtcValid, float batteryVoltage);
void recordGeofenceStatus(bool exitEvent, bool enterEvent);
void recordTelemetryStatus(bool mqttSuccess, bool httpSuccess);
void updateDeviceStatus(bool networkAvailable, bool networkConnecting, bool mqttConnected);
void recordSMSTelemetryStatus(bool acceptedByModem);
void updateSMSDeviceStatus(bool registered, bool connecting, bool healthy, bool pending, bool fault);

#endif
