#ifndef DS18B20_H
#define	DS18B20_H

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

/** Standard libs **/
#include <stdint.h>

/** Custom libs **/
#include "OneWire.h"
#include "Edc.h"

/******************************************************************************/
/*---------------------------------Macros-------------------------------------*/
/******************************************************************************/

/** Number of iterations if CRC validation fails **/
#define DS_READ_RAM_REPEAT_COUNT        3       // Scratch-pad read
#define DS_SEARCH_DEVICE_REPEAT_COUNT   3       // Search device ID

/** Timeout for polling-based operations **/
#define DS_SAVE_COPY_ROM_TIMEOUT_MS     100
#define DS_CONV_TEMP_TIMEOUT_MS         1000    // Must be more than 755 ms
#define DS_SEARCH_ID_TIMEOUT_MS         1000

/******************************************************************************/
/*----------------------------Enumeration Types-------------------------------*/
/******************************************************************************/

typedef enum {
    DS_MEAS_RES_9BIT = 0,
    DS_MEAS_RES_10BIT = 1,
    DS_MEAS_RES_11BIT = 2,
    DS_MEAS_RES_12BIT = 3
} DsMeasRes_t;

/******************************************************************************/
/*-----------------------------Data Structures--------------------------------*/
/******************************************************************************/

/** DS18B20 configuration parameters **/
typedef struct {
    OwConfig_t      owConfig;
    DsMeasRes_t     measRes;
    uint64_t        deviceId;   // Only applicable for single device config mode
    int             lowAlarm;
    int             highAlarm;
} DsConfig_t;

/******************************************************************************/
/*---------------------------- Function Prototypes----------------------------*/
/******************************************************************************/

/** Search functions **/
uint32_t DS18B20_SearchDeviceId(const uint32_t pinCode, uint64_t *romIdBuff);
uint32_t DS18B20_SearchAlarm(const uint32_t pinCode, uint64_t *romIdBuff);

/** Configuration functions **/
bool DS18B20_ConfigDevice(DsConfig_t dsConfig, bool isMultiMode);
bool DS18B20_SaveToRom(const uint64_t *romId, bool isMultiMode);
bool DS18B20_CopyFromRom(const uint64_t *romId, bool isMultiMode);
bool DS18B20_SetCorrection(float corr);

/** Operation functions **/
bool DS18B20_IsConvDone(void);
bool DS18B20_ConvertReadTemp(const uint64_t *romId, float *dataBuff, const uint8_t deviceCount);
bool DS18B20_ConvertTemp(const uint64_t *romId, const uint32_t deviceCount);
bool DS18B20_ReadTemp(const uint64_t *romId, float *dataBuff, const uint32_t deviceCount);
bool DS18B20_ReadRam(const uint64_t *romId, int *dataBuff, const uint32_t deviceCount);

/** Other functions **/
bool DS18B20_IsDeviceFake(const uint64_t *romId);

#endif	/* DS18B20_H */