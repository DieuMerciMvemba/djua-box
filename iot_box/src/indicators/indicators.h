#ifndef DJUA_INDICATORS_H
#define DJUA_INDICATORS_H

enum SystemIndicatorState { SYSTEM_BOOTING, SYSTEM_RUNNING };
enum CommunicationIndicatorState {
    COMM_OFFLINE, COMM_CONNECTING, COMM_TRANSPORT_PENDING, COMM_ONLINE
};
enum AlertIndicatorState { ALERT_NONE, ALERT_WARNING, ALERT_CRITICAL, ALERT_GEOFENCE };
enum BuzzerEvent { BUZZER_NONE, BUZZER_INFO, BUZZER_WARNING, BUZZER_CRITICAL, BUZZER_GEOFENCE };

void initIndicators();
void updateIndicators();
void setSystemIndicatorState(SystemIndicatorState state);
void setCommunicationIndicatorState(CommunicationIndicatorState state);
void setAlertIndicatorState(AlertIndicatorState state);
void triggerBuzzer(BuzzerEvent event);
SystemIndicatorState getSystemIndicatorState();
CommunicationIndicatorState getCommunicationIndicatorState();
AlertIndicatorState getAlertIndicatorState();

#endif
