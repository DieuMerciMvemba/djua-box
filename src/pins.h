#ifndef PINS_H
#define PINS_H

// =====================================================
// IOT BOX - BROCHES ESP32
// =====================================================

// ---------- INA219 / I2C ----------
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// ---------- GPS NEO-6M / UART2 ----------

#define GPS_RX_PIN 16
#define GPS_TX_PIN 17

// ---------- RTC DS1302 / THREE-WIRE ----------

#define DS1302_DATA_PIN 26
#define DS1302_CLOCK_PIN 25
#define DS1302_CE_PIN 27

// ---------- MESURE CHARGE AC ----------
// ADC1 reste utilisable pendant que le Wi-Fi de l'ESP32 est actif.
#define ZMPT101B_ADC_PIN 34
#define ACS712_ADC_PIN 35

// ---------- INDICATION LOCALE DJUA ----------
#define LED_SYSTEM_PIN 18
#define LED_COMM_PIN 19
#define LED_ALERT_PIN 23
#define BUZZER_PIN 33

// ---------- SIM800C : UART1 RX32 / TX13 ----------
// Cablage confirme au banc par l'utilisateur : TXD modem -> RX32,
// RXD modem -> TX13, masse commune. UART2 GPIO16/17 reste au GPS.
// Aucun PWRKEY n'est suppose par le firmware.
#ifndef GSM_RX_PIN
#define GSM_RX_PIN 32
#endif
#ifndef GSM_TX_PIN
#define GSM_TX_PIN 13
#endif
// -1 : carte a demarrage automatique/manuellement mise sous tension.
// Sinon piloter PWRKEY par transistor, apres validation du schema de la carte.
#ifndef GSM_PWRKEY_PIN
#define GSM_PWRKEY_PIN -1
#endif

// ---------- FUTURE MICROSD ----------
// #define SD_CS_PIN 

#endif
