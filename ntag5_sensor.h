#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    Ntag5SensorStatusWaiting, /**< No tag in the field */
    Ntag5SensorStatusOk, /**< temperature is valid */
    Ntag5SensorStatusNotSensor, /**< A tag answered, but it is not an NTAG 5 link sensor */
    Ntag5SensorStatusNoPower, /**< Energy harvesting is disabled, the sensor gets no power */
    Ntag5SensorStatusNoValue, /**< The sensor returned its reset value, no conversion */
} Ntag5SensorStatus;

typedef struct {
    Ntag5SensorStatus status;
    float temperature; /**< Skin temperature in degrees Celsius */
    uint16_t raw; /**< TMP117 temperature register */
} Ntag5SensorReading;

/** Called from the NFC worker thread after every attempt */
typedef void (*Ntag5SensorCallback)(const Ntag5SensorReading* reading, void* context);

typedef struct Ntag5Sensor Ntag5Sensor;

Ntag5Sensor* ntag5_sensor_alloc(void);

void ntag5_sensor_free(Ntag5Sensor* instance);

/** Start polling for a sensor tag and measuring about once per second */
void ntag5_sensor_start(Ntag5Sensor* instance, Ntag5SensorCallback callback, void* context);

/** Stop polling, no callbacks after this returns */
void ntag5_sensor_stop(Ntag5Sensor* instance);

#ifdef __cplusplus
}
#endif
