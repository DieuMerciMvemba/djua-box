#pragma once
#include "d2_codec.h"
#include "d2_store.h"

// Facade Arduino : la logique testable reste dans d2_* et sms_scheduler.
void initSMS();
void updateSMS();
bool enqueueSMS(djua::D2Frame frame);
bool isSMSNetworkRegistered();
bool isSMSConnecting();
bool isSMSHardwareConfigured();
bool isSMSHealthy();
bool isSMSFaulted();
bool hasPendingSMS();
djua::D2State smsProvisioningState();
