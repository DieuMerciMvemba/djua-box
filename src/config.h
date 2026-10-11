#ifndef CONFIG_H
#define CONFIG_H

// IOT BOX - CONFIGURATION GENERALE
//  IDENTIFICATION BACKEND 
#define DEVICE_ID "DJUA-KIN-000001"

//  MONITEUR SERIE 
#define SERIAL_BAUD_RATE 115200

// Target V2; set 0 only for explicitly legacy hardware benches.
#ifndef IOT_HARDWARE_V2
#define IOT_HARDWARE_V2 1
#endif

// Un seul transport actif. Surcharge possible a la compilation avec -D.
#define DJUA_PROFILE_SMS_PRODUCTION 1
#define DJUA_PROFILE_MQTT_TEST 2
#define DJUA_PROFILE_HTTP_TEST 3
#ifndef DJUA_COMM_PROFILE
#define DJUA_COMM_PROFILE DJUA_PROFILE_SMS_PRODUCTION
#endif
#if DJUA_COMM_PROFILE < 1 || DJUA_COMM_PROFILE > 3
#error "DJUA_COMM_PROFILE doit etre SMS_PRODUCTION, MQTT_TEST ou HTTP_TEST"
#endif

#define D2_REFERENCE_SHA "c5835077b2b6e8418e34898013cacc0892c12e25"
#define ELECTRICAL_SAMPLE_INTERVAL_MS 10000UL
#define D2_ELECTRICAL_MAX_AGE_MS 20000UL
#define D2_ENERGY_MAX_GAP_MS 20000UL
#ifndef D2_PERIOD_SECONDS
#define D2_PERIOD_SECONDS 60UL
#endif
static_assert(D2_PERIOD_SECONDS >= 10UL && D2_PERIOD_SECONDS <= 86400UL,
              "La periode D2 doit rester entre 10 et 86400 secondes");

// Ancien SIM800C uniquement : carte marquee DC:5V-9V IN ; validation non applicable a l'A7670.
// Banc UART RX32/TX13 valide par l'utilisateur ; alimentation a dimensionner.
// UART GSM actif en SMS_PRODUCTION ; carte deja allumee, sans GPIO PWRKEY.
#ifndef GSM_HARDWARE_CONFIGURED
#define GSM_HARDWARE_CONFIGURED (!IOT_HARDWARE_V2)
#endif
#ifndef GSM_BAUD_RATE
#define GSM_BAUD_RATE (IOT_HARDWARE_V2 ? 115200UL : 9600UL)
#endif
#ifndef GSM_MODULE_LABEL
#if IOT_HARDWARE_V2
#define GSM_MODULE_LABEL "A7670"
#else
#define GSM_MODULE_LABEL "SIM800C"
#endif
#endif
// PWRKEY optionnel via transistor : niveau actif cote ESP32, pas cote module.
#ifndef GSM_PWRKEY_ACTIVE_LEVEL
#define GSM_PWRKEY_ACTIVE_LEVEL 1
#endif
#define GSM_POWER_STABILIZE_MS 1000UL
#define GSM_PWRKEY_PULSE_MS 1200UL
#define GSM_BOOT_WAIT_MS 5000UL
#define GSM_COMMAND_TIMEOUT_MS (IOT_HARDWARE_V2 ? 10000UL : 5000UL)
#define GSM_SUBMIT_TIMEOUT_MS (IOT_HARDWARE_V2 ? 120000UL : 60000UL)
#define GSM_RETRY_INTERVAL_MS 30000UL
#define SMS_RETRY_MAX_MS 900000UL
static_assert(GSM_COMMAND_TIMEOUT_MS > 0 && GSM_SUBMIT_TIMEOUT_MS > 0 &&
              GSM_RETRY_INTERVAL_MS > 0 && SMS_RETRY_MAX_MS >= GSM_RETRY_INTERVAL_MS,
              "Les temporisations GSM/SMS doivent etre positives et coherentes");
// Active seulement pendant un provisionnement local supervise par USB.
#ifndef D2_PROVISIONING_ENABLED
#define D2_PROVISIONING_ENABLED 0
#endif

// Convention D2 : positif = charge, negatif = decharge. A verifier au banc.
#ifndef BATTERY_CURRENT_POLARITY
#define BATTERY_CURRENT_POLARITY 1
#endif
static_assert(BATTERY_CURRENT_POLARITY == 1 || BATTERY_CURRENT_POLARITY == -1,
              "La polarite batterie doit etre 1 ou -1");

//  INA219 BATTERIE 
#define INA219_I2C_ADDRESS 0x40
#define INA219_I2C_CLOCK_HZ 100000UL
#define INA219_SAMPLE_COUNT 7U
#define INA219_SAMPLE_DELAY_MS 3UL
// Plage de validite correspondant a la batterie nominale 12 V du boitier.
#define BATTERY_MIN_VALID_V 9.0F
#define BATTERY_MAX_VALID_V 16.5F

// INA219 SOLAIRE : adresse a confirmer avec le scanner, sur le bus commun.
#ifndef SOLAR_MEASUREMENT_ENABLED
#define SOLAR_MEASUREMENT_ENABLED 0
#endif
#define SOLAR_INA219_I2C_ADDRESS 0x41
#define SOLAR_INA219_SAMPLE_COUNT 7U
#define SOLAR_INA219_SAMPLE_DELAY_MS 3UL
#ifndef SOLAR_CURRENT_POLARITY
#define SOLAR_CURRENT_POLARITY 1
#endif
static_assert(
    SOLAR_INA219_I2C_ADDRESS != INA219_I2C_ADDRESS,
    "INA219 batterie et solaire doivent avoir des adresses differentes."
);

// Troisieme INA219 : charge DC, shunt EXTERNE. Valeurs exemples, pas un
// choix materiel valide. Mettre CONFIGURED=1 seulement apres montage/verifications.
#ifndef DC_LOAD_MEASUREMENT_ENABLED
#define DC_LOAD_MEASUREMENT_ENABLED 1
#endif
#ifndef DC_LOAD_SHUNT_CONFIGURED
#define DC_LOAD_SHUNT_CONFIGURED 0
#endif
#ifndef DC_LOAD_INA219_I2C_ADDRESS
#define DC_LOAD_INA219_I2C_ADDRESS 0x44
#endif
#ifndef DC_LOAD_SHUNT_RATED_A
#define DC_LOAD_SHUNT_RATED_A 50.0
#endif
#ifndef DC_LOAD_SHUNT_RATED_MV
#define DC_LOAD_SHUNT_RATED_MV 75.0
#endif
#ifndef DC_LOAD_MAX_VALID_V
#define DC_LOAD_MAX_VALID_V 26.0
#endif
#ifndef DC_LOAD_CURRENT_POLARITY
#define DC_LOAD_CURRENT_POLARITY 1
#endif
#define DC_LOAD_INA219_SAMPLE_COUNT 7U
#define DC_LOAD_INA219_SAMPLE_DELAY_MS 3UL
static_assert(DC_LOAD_INA219_I2C_ADDRESS >= 0x40 && DC_LOAD_INA219_I2C_ADDRESS <= 0x4F &&
              DC_LOAD_INA219_I2C_ADDRESS != INA219_I2C_ADDRESS &&
              DC_LOAD_INA219_I2C_ADDRESS != SOLAR_INA219_I2C_ADDRESS,
              "Les trois INA219 doivent avoir des adresses distinctes");
static_assert(DC_LOAD_SHUNT_RATED_A > 0 && DC_LOAD_SHUNT_RATED_A <= 100 &&
              DC_LOAD_SHUNT_RATED_MV > 0 && DC_LOAD_SHUNT_RATED_MV < 320 &&
              DC_LOAD_MAX_VALID_V > 0 && DC_LOAD_MAX_VALID_V <= 26,
              "Shunt/plage DC incompatibles avec INA219 ou D2T2");
static_assert(DC_LOAD_CURRENT_POLARITY == 1 || DC_LOAD_CURRENT_POLARITY == -1,
              "La polarite DC doit etre 1 ou -1");

//  MESURE CHARGE AC : ZMPT101B + ACS712-30A
// Chaque sortie 0-5 V doit passer par son propre diviseur 10k/10k avant
// l'entree ADC ESP32. Les valeurs fines sont ensuite calibrables par USB.
#define AC_MEASUREMENT_ENABLED 1
#define ZMPT101B_VOLTAGE_GAIN_V_PER_ADC_MV 1.0000F
#define ACS712_30A_SENSITIVITY_MV_PER_A 66.0F
#define AC_OUTPUT_DIVIDER_RATIO 0.5000F
#define AC_CURRENT_GAIN_A_PER_ADC_MV \
    (1.0F / (ACS712_30A_SENSITIVITY_MV_PER_A * AC_OUTPUT_DIVIDER_RATIO))
#define AC_PHASE_CORRECTION_US 0
#define AC_CURRENT_POLARITY 1
#define AC_CURRENT_NOISE_FLOOR_A 0.150F
#define AC_MEASUREMENT_WINDOW_MS 1000UL
#define AC_SAMPLE_PERIOD_US 300U
#define AC_MINIMUM_SAMPLE_COUNT 400U
#define AC_MAXIMUM_SAMPLE_COUNT 4096U

//  GPS NEO-8M (NEO-6M conserve pour les bancs historiques)
#define GPS_BAUD_RATE 9600
// Age maximal d'une position utilisable par la telemetrie.
#define GPS_MAX_AGE_MS 5000UL

// V2 sensor configuration. Enable after checking the physical board, straps,
// wiring, current limits and shunt rating. Defaults never assert commissioning.
#define V2_I2C_CLOCK_HZ 100000UL
#ifndef V2_BATTERY_CONFIGURED
#define V2_BATTERY_CONFIGURED 0
#endif
#ifndef V2_SOLAR_CONFIGURED
#define V2_SOLAR_CONFIGURED 0
#endif
#ifndef V2_DC_CONFIGURED
#define V2_DC_CONFIGURED 0
#endif
#ifndef V2_TV_CONFIGURED
#define V2_TV_CONFIGURED 0
#endif
#ifndef PZEM_HARDWARE_CONFIGURED
#define PZEM_HARDWARE_CONFIGURED 0
#endif
#ifndef D3_RECEIVER_ENABLED
#define D3_RECEIVER_ENABLED 0
#endif
#ifndef V2_BATTERY_ADDRESS
#define V2_BATTERY_ADDRESS 0x40
#endif
#ifndef V2_SOLAR_ADDRESS
#define V2_SOLAR_ADDRESS 0x41
#endif
#ifndef V2_DC_ADDRESS
#define V2_DC_ADDRESS 0x42
#endif
#ifndef V2_TV_ADDRESS
#define V2_TV_ADDRESS 0x44
#endif
// 30 A / 75 mV = 2.5 mOhm. Three independent physical resistances.
#ifndef BATTERY_SHUNT_OHMS
#define BATTERY_SHUNT_OHMS 0.0025
#endif
#ifndef SOLAR_SHUNT_OHMS
#define SOLAR_SHUNT_OHMS 0.0025
#endif
#ifndef TV_SHUNT_OHMS
#define TV_SHUNT_OHMS 0.0025
#endif
#ifndef BATTERY_MAX_CURRENT_A
#define BATTERY_MAX_CURRENT_A 30.0
#endif
#ifndef SOLAR_MAX_CURRENT_A
#define SOLAR_MAX_CURRENT_A 30.0
#endif
#ifndef TV_MAX_CURRENT_A
#define TV_MAX_CURRENT_A 30.0
#endif
// 3 W is a required minimum assumed for configuration, not a measured rating.
#ifndef BATTERY_SHUNT_RATED_W
#define BATTERY_SHUNT_RATED_W 3.0
#endif
#ifndef SOLAR_SHUNT_RATED_W
#define SOLAR_SHUNT_RATED_W 3.0
#endif
#ifndef TV_SHUNT_RATED_W
#define TV_SHUNT_RATED_W 3.0
#endif
#ifndef BATTERY_ADC_NARROW_RANGE
#define BATTERY_ADC_NARROW_RANGE 0
#endif
#ifndef SOLAR_ADC_NARROW_RANGE
#define SOLAR_ADC_NARROW_RANGE 0
#endif
#ifndef TV_ADC_NARROW_RANGE
#define TV_ADC_NARROW_RANGE 0
#endif
#ifndef SOLAR_MAX_BUS_V
#define SOLAR_MAX_BUS_V 85.0
#endif
#ifndef TV_MAX_BUS_V
#define TV_MAX_BUS_V 26.0
#endif
#ifndef TV_CURRENT_POLARITY
#define TV_CURRENT_POLARITY 1
#endif
#ifndef DC1_SHUNT_OHMS
#define DC1_SHUNT_OHMS 0.010
#endif
#ifndef DC2_SHUNT_OHMS
#define DC2_SHUNT_OHMS 0.010
#endif
#ifndef DC3_SHUNT_OHMS
#define DC3_SHUNT_OHMS 0.010
#endif
// Provisional 5 A limits and 1 W minimum rating; verify each physical port.
#ifndef DC1_MAX_CURRENT_A
#define DC1_MAX_CURRENT_A 5.0
#endif
#ifndef DC2_MAX_CURRENT_A
#define DC2_MAX_CURRENT_A 5.0
#endif
#ifndef DC3_MAX_CURRENT_A
#define DC3_MAX_CURRENT_A 5.0
#endif
#ifndef DC1_SHUNT_RATED_W
#define DC1_SHUNT_RATED_W 1.0
#endif
#ifndef DC2_SHUNT_RATED_W
#define DC2_SHUNT_RATED_W 1.0
#endif
#ifndef DC3_SHUNT_RATED_W
#define DC3_SHUNT_RATED_W 1.0
#endif
#ifndef DC1_CURRENT_POLARITY
#define DC1_CURRENT_POLARITY 1
#endif
#ifndef DC2_CURRENT_POLARITY
#define DC2_CURRENT_POLARITY 1
#endif
#ifndef DC3_CURRENT_POLARITY
#define DC3_CURRENT_POLARITY 1
#endif
#define PZEM_BAUD_RATE 9600UL
#ifndef PZEM_MODBUS_ADDRESS
#define PZEM_MODBUS_ADDRESS 0xF8
#endif
#define PZEM_POLL_INTERVAL_MS 2000UL
#define PZEM_MAX_AGE_MS 5000UL
#define PZEM_REPLY_TIMEOUT_MS 1000UL
#if IOT_HARDWARE_V2
static_assert(V2_BATTERY_ADDRESS != V2_SOLAR_ADDRESS && V2_BATTERY_ADDRESS != V2_TV_ADDRESS &&
    V2_BATTERY_ADDRESS != V2_DC_ADDRESS && V2_SOLAR_ADDRESS != V2_TV_ADDRESS &&
    V2_SOLAR_ADDRESS != V2_DC_ADDRESS && V2_TV_ADDRESS != V2_DC_ADDRESS, "Conflit I2C V2");
static_assert(BATTERY_MAX_CURRENT_A <= 30 && SOLAR_MAX_CURRENT_A <= 30 && TV_MAX_CURRENT_A <= 30 &&
    DC1_MAX_CURRENT_A <= 30 && DC2_MAX_CURRENT_A <= 30 && DC3_MAX_CURRENT_A <= 30,
    "Courants V2 hors du contrat D3");
#endif

//  GEOFENCING LOCAL
#define GEOFENCE_ENABLED 1
#define GEOFENCE_CENTER_LAT (-4.3308595)
#define GEOFENCE_CENTER_LON (15.30678333)
#define GEOFENCE_ENTER_RADIUS_M 10.0F
#define GEOFENCE_EXIT_RADIUS_M 15.0F
#define GEOFENCE_CHECK_INTERVAL_MS 5000UL
#define GEOFENCE_CONFIRM_COUNT 3U

//  HORLOGE RTC DS1302
// Le DS1302 conserve l'heure locale correspondant a ce fuseau fixe.
#define RTC_GMT_OFFSET_MINUTES 60
#define RTC_TIMEZONE_LABEL "GMT+1"
#ifndef RTC_AUTO_INITIALIZE_IF_INVALID
// Production : ne pas presenter l'heure de compilation comme une heure fiable.
#define RTC_AUTO_INITIALIZE_IF_INVALID (DJUA_COMM_PROFILE != DJUA_PROFILE_SMS_PRODUCTION)
#endif

// Wi-Fi
#define WIFI_SSID "ODC_LOCAL"
// Two literal backslashes in the password are represented by four in this C++ string.
#define WIFI_PASSWORD "SecureTopTop=ABC_Puits"

//BACKEND REEL
#define BACKEND_URL "http://10.252.252.40:5000/api/iot/telemetry"
#define IOT_API_KEY "djua"
#define ENABLE_HTTP_BACKEND (DJUA_COMM_PROFILE == DJUA_PROFILE_HTTP_TEST)

// MQTT TESTING
// Public broker for short-term integration tests only. Do not use this broker
// or these public topics for production or sensitive telemetry.
#define MQTT_BROKER_HOST "test.mosquitto.org"
#define MQTT_BROKER_PORT 1883
#define MQTT_TOPIC_PREFIX "djua/test"
#define MQTT_RETRY_INTERVAL_MS 10000UL

#define TELEMETRY_INTERVAL_MS 10000UL
#define WIFI_RETRY_INTERVAL_MS 10000UL

// Indication locale : LEDs cablees vers GND avec resistance serie.
#define LED_ACTIVE_HIGH 1
#define SYSTEM_BOOT_BLINK_MS 200UL
#define SYSTEM_HEARTBEAT_PERIOD_MS 2000UL
#define SYSTEM_HEARTBEAT_ON_MS 100UL
#define COMM_SLOW_BLINK_MS 500UL
#define COMM_FAST_BLINK_MS 150UL
#define ALERT_WARNING_BLINK_MS 500UL
#define ALERT_CRITICAL_BLINK_MS 150UL

// Surcharges possibles pour le sketch materiel et les compilations de test.
#ifndef BUZZER_ENABLED
#define BUZZER_ENABLED 1
#endif
#ifndef BUZZER_ACTIVE_HIGH
#define BUZZER_ACTIVE_HIGH 1
#endif
#ifndef BUZZER_PASSIVE
#define BUZZER_PASSIVE 1
#endif
#define BUZZER_FREQUENCY_HZ 2000UL
#define BUZZER_SHORT_BEEP_MS 100UL
#define BUZZER_LONG_BEEP_MS 700UL
#define BUZZER_GAP_MS 150UL
#define BUZZER_GEOFENCE_GAP_MS 200UL
#define BUZZER_GEOFENCE_SHORT_MS 150UL
#define BUZZER_SERVICE_PERIOD_MS 10UL

// Compteurs de cycles de mesure, apres la recuperation propre aux capteurs.
#define SENSOR_WARNING_FAILURE_COUNT 3U
#define INA219_CRITICAL_FAILURE_COUNT 3U
#define TELEMETRY_WARNING_FAILURE_COUNT 3U
#define MQTT_WARNING_GRACE_MS 30000UL
#define HTTP_SUCCESS_VALID_MS (3UL * TELEMETRY_INTERVAL_MS)
#define WIFI_CONNECT_TIMEOUT_MS 15000UL

// Aucun seuil metier batterie n'est suppose. Pour activer, definir les deux
// tensions selon la technologie et les exigences de la batterie installee.
#define BATTERY_ALERTS_ENABLED 0
// #define BATTERY_WARNING_V ...
// #define BATTERY_CRITICAL_V ...
#if BATTERY_ALERTS_ENABLED
#if !defined(BATTERY_WARNING_V) || !defined(BATTERY_CRITICAL_V)
#error "Configurer BATTERY_WARNING_V et BATTERY_CRITICAL_V avant activation"
#endif
#endif

#endif
