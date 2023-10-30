#include "DS18B20.h"

/** DS18B20 Family Code **/
#define DS18B20_FAMILY_CODE     0x28   // Code 0x10 for DS18S20 (not supported)

/** DS18B20 CRC Polynomial **/
#define CRC_POLY_SIZE           8
#define CRC_POLY_CODE           0x31

/** DS18B20 ROM Commands **/
#define SEARCH_ROM_CMD          0xF0
#define READ_ROM_CMD            0x33
#define MATCH_ROM_CMD           0x55
#define SKIP_ROM_CMD            0xCC
#define ALARM_SEARCH_CMD        0xEC

/** DS18B20 Function Commands **/
#define CONV_TEMP_CMD           0x44
#define WRITE_MEM_CMD           0x4E
#define READ_MEM_CMD            0xBE
#define COPY_MEM_CMD            0x48
#define RECALL_EEPROM_CMD       0xB8
#define READ_POWER_CMD          0xB4

/** Limit temperature values **/
#define MAX_TEMP                127
#define MIN_TEMP               -55

/******************************************************************************/
/*--------------------------Local Data Structures-----------------------------*/
/******************************************************************************/

/** Static structure **/
static struct {
    uint32_t            sysFreq;
    uint32_t            owPinCode;
    float               tempCorr;
} statVar;

/** Enumeration types **/
typedef enum {SEARCH_DEVICE_ID, SEARCH_DEVICE_ALARM } SearchMode_t;
typedef enum {SAVE_ROM_MODE, COPY_ROM_MODE} RomMode_t;

/******************************************************************************/
/*------------------------Local Function Prototypes---------------------------*/
/******************************************************************************/

static uint32_t SearchDevice(const uint32_t pinCode, uint64_t *romIdBuff, SearchMode_t searchMode);
static bool GenerateCrcLut(void);
static bool ConfigDevice(DsConfig_t dsConfig, bool isMultiMode);
static bool SaveCopyRom(const uint64_t *romId, bool isMultiMode, RomMode_t romMode);

/******************************************************************************/
/*----------------------External Function Definitions-------------------------*/
/******************************************************************************/

/*
 *  Scan and identify all DS18B20 devices on OW bus
 */
extern uint32_t DS18B20_SearchDeviceId(const uint32_t pinCode, uint64_t *romIdBuff)
{
    return SearchDevice(pinCode, romIdBuff, SEARCH_DEVICE_ID);
}


/*
 *  Scan check alarm flags for all DS18B20 devices on OW bus
 */
extern uint32_t DS18B20_SearchAlarm(const uint32_t pinCode, uint64_t *romIdBuff)
{
    return SearchDevice(pinCode, romIdBuff, SEARCH_DEVICE_ALARM);
}


/*
 *  Configure any amount of DS18B20 devices
 */
extern bool DS18B20_ConfigDevice(DsConfig_t dsConfig, bool isMultiMode)
{
    /* Set up OneWire bus */
    OW_ConfigBus(dsConfig.owConfig);
    
    /* Generate CRC LUT once for active use */
    if (!GenerateCrcLut())
    {
        return false;
    }
 
    /* Modify static structure */
    statVar.sysFreq = OSC_GetSysFreq();
    statVar.owPinCode = dsConfig.owConfig.pinCode;
    
    /* Modify configuration register */
    return ConfigDevice(dsConfig, isMultiMode);
}


/*
 *  Saves alarm and resolution settings from RAM to EEPROM
 */
extern bool DS18B20_SaveToRom(const uint64_t *romId, bool isMultiMode)
{
    return SaveCopyRom(romId, isMultiMode, SAVE_ROM_MODE);
}


/*
 *  Reloads alarm and resolution settings from EEPROM to RAM
 */
extern bool DS18B20_CopyFromRom(const uint64_t *romId, bool isMultiMode)
{
    return SaveCopyRom(romId, isMultiMode, COPY_ROM_MODE);
}


/*
 *  Set a correction for temperature calculation for all devices
 */
extern bool DS18B20_SetCorrection(float corr)
{
    if ((corr < -55.0) || (corr > 125.0))
    {
        return false;
    }
    
    statVar.tempCorr = corr;
    return true;
}


/*
 *  Check if device is fake (has fixed conversion resolution and time)
 */
extern bool DS18B20_IsDeviceFake(const uint64_t *romId)
{
    /* Presence check */
    if (!OW_Reset(statVar.owPinCode))
    {
        return false;
    }
    
    /* Input check */
    if ((*romId == 0) || (romId == NULL))
    {
        return false;
    }
    
    /* ROM access code */
    uint64_t romData = (*romId << 8) | DS18B20_FAMILY_CODE;
    uint64_t crcData = EDC_CalculateCrc(CRC_POLY_CODE, &romData, 7);
    romData |= (crcData << 56);
    
    /* Match ROM */
    OW_WriteByte(statVar.owPinCode, MATCH_ROM_CMD);
    OW_WriteMultiByte(statVar.owPinCode, &romData, 8);
    
    /* Configure to 9-bit resolution (95 ms per conversion) */
    uint8_t measRes = (DS_MEAS_RES_9BIT << 5);
    uint8_t txData[3] = {0x00, 0x00, measRes};
    OW_WriteByte(statVar.owPinCode, WRITE_MEM_CMD);
    OW_WriteMultiByte(statVar.owPinCode, txData, 3);
    
    /* Re-initialize bus */
    if (!OW_Reset(statVar.owPinCode))
    {
        return false;
    }
    
    /* Match ROM + convert */
    OW_WriteByte(statVar.owPinCode, MATCH_ROM_CMD);
    OW_WriteMultiByte(statVar.owPinCode, &romData, 8);
    OW_WriteByte(statVar.owPinCode, CONV_TEMP_CMD);
    
    /* SYSCLK default value */
    if (statVar.sysFreq == 0)
    {
        statVar.sysFreq = 8000000;
    }
    
    /* Use Core timer for timeout */
    uint32_t delay = 95 * (statVar.sysFreq / 1000 / 2);
    uint32_t timeout = _CP0_GET_COUNT() + delay;
    
    /* Wait for conversion done */
    while (timeout > _CP0_GET_COUNT());
    
    /* Check if not done after 95 ms */
    if (!OW_ReadBit(statVar.owPinCode))
    {
        return true;
    }
    
    return false;
}


/*
 *  Check if conversion done
 */
extern bool DS18B20_IsConvDone(void)
{
    /* Conversion in progress check */
    if (!OW_ReadBit(statVar.owPinCode))
    {
        return false;
    }
    
    return true;
}


/*
 *  Convert and read temperature with timeout
 */
extern bool DS18B20_ConvertReadTemp(const uint64_t *romId, float *dataBuff, const uint8_t deviceCount)
{
    /* Input check */
    if (romId == NULL)
    {
        return false;
    }
    /* Start temperature conversion */
    if (!DS18B20_ConvertTemp(romId, deviceCount))
    {
        return false;
    }
    
    /* SYSCLK default value */
    if (statVar.sysFreq == 0)
    {
        statVar.sysFreq = 8000000;
    }

    /* Use Core timer for timeout */
    uint32_t delay = DS_CONV_TEMP_TIMEOUT_MS * (statVar.sysFreq / 1000 / 2);
    uint32_t timeout = _CP0_GET_COUNT() + delay;
    
    /* Wait for conversion done or timeout */
    while (!DS18B20_IsConvDone() && (timeout > _CP0_GET_COUNT()));
    
    /* Timeout check */
    if (timeout < _CP0_GET_COUNT())
    {
        return false;
    }
    
    /* Read and convert raw data */
    return DS18B20_ReadTemp(romId, dataBuff, deviceCount);
}


/*
 *  Convert temperature
 */
extern bool DS18B20_ConvertTemp(const uint64_t *romId, const uint32_t deviceCount)
{
    /* Inputs check (romId NULL allowed in multi-device mode) */
    if (deviceCount == 0)
    {
        return false;
    }
    
    /* Single device configuration ROM check */
    if (((*romId == 0) || (romId == NULL)) && (deviceCount == 1))
    {
        return false;
    }
    
    /* Presence check */
    if (!OW_Reset(statVar.owPinCode))
    {
        return false;
    }
    
    /* Single device mode */
    if (deviceCount == 1)
    {
        /* Generate ROM access code */
        uint64_t romData = (*romId << 8) | DS18B20_FAMILY_CODE;
        uint64_t crcData = EDC_CalculateCrc(CRC_POLY_CODE, &romData, 7);
        romData |= (crcData << 56);
        
        OW_WriteByte(statVar.owPinCode, MATCH_ROM_CMD);
        OW_WriteMultiByte(statVar.owPinCode, &romData, 8);
    }
    /* Multi device mode */
    else
    {
        OW_WriteByte(statVar.owPinCode, SKIP_ROM_CMD);
    }
    
    OW_WriteByte(statVar.owPinCode, CONV_TEMP_CMD);
    
    return true;
}


/*
 *  Read converted temperature data
 */
extern bool DS18B20_ReadTemp(const uint64_t *romId, float *dataBuff, const uint32_t deviceCount)
{    
    /* Inputs check */
    if ((romId == NULL) || (dataBuff == NULL) || (deviceCount == 0))
    {
        return false;
    }
    
    /* Single device configuration ROM check */
    if ((*romId == 0) && (deviceCount == 1))
    {
        return false;
    }
    
    /* Conversion done check */
    if (!OW_ReadBit(statVar.owPinCode))
    {
        return false;
    }

    uint64_t romData, crcData;
    uint8_t rxData[deviceCount][9];
    uint8_t idxb, idxa = 0;
    
    /* Repeat data read if CRC fails */
    do
    {
        for (idxb = 0; idxb < deviceCount; idxb++)
        {
            /* Re-initialize bus */
            if (!OW_Reset(statVar.owPinCode))
            {
                return false;
            }
            
            /* Generate ROM access code */
            romData = (*romId << 8) | DS18B20_FAMILY_CODE;
            crcData = EDC_CalculateCrc(CRC_POLY_CODE, &romData, 7);
            romData |= (crcData << 56);
            romId++;

            /* Match ROM */
            OW_WriteByte(statVar.owPinCode, MATCH_ROM_CMD);
            OW_WriteMultiByte(statVar.owPinCode, &romData, 8);

            /* Read scratch-pad */
            OW_WriteByte(statVar.owPinCode,READ_MEM_CMD);
            OW_ReadMultiByte(statVar.owPinCode, &rxData[idxb][0], 9);
                       
            /* Invalid data receive check */
            if (EDC_CalculateCrc(CRC_POLY_CODE, &rxData[idxb][0], 9) != 0)
            {
                break;
            }
        }
        
        /* All device's CRC valid */
        if (idxb == deviceCount)
        {
            break;
        }
        
        idxa++;
        
    } while (idxa < DS_READ_RAM_REPEAT_COUNT);
    
    /* Broken connection or CRC invalid and neither of devices responded */
    if ((idxa >= (DS_READ_RAM_REPEAT_COUNT - 1)) && (idxb == 0))
    {
        return false;
    }
    
    uint8_t intgr, frctn;
    float signPart;

    /* Convert raw data to Celsius */
    for (uint8_t idx = 0; idx < deviceCount; idx++)
    {
        intgr = ((rxData[idx][1] & 0x07) << 4) | (rxData[idx][0] >> 4);
        frctn = rxData[idx][0] & 0x0F;
        signPart = (rxData[idx][1] & 0x08) ? (-1.0) : (1.0);
        
        *dataBuff = ((float)intgr + (float)frctn / 16) * signPart + statVar.tempCorr;
        dataBuff++;
    }
    
    return true;
}


/*
 *  Read alarm and resolution data from scratch-pad
 */
extern bool DS18B20_ReadRam(const uint64_t *romId, int *dataBuff, const uint32_t deviceCount)
{ 
    /* Inputs check */
    if ((dataBuff == NULL) || (deviceCount == 0))
    {
        return false;
    }
    
    /* Single device configuration ROM check */
    if (((*romId == 0) || (romId == NULL)) && (deviceCount == 1))
    {
        return false;
    }
    
    /* Check if bus busy */
    if (!OW_ReadBit(statVar.owPinCode))
    {
        return false;
    }

    uint64_t romData, crcData;
    uint8_t rxData[deviceCount][9];
    uint8_t idxb, idxa = 0;
    
    uint8_t loIntgr, hiIntgr;
    int loSign, hiSign;
    
    /* Repeat data read if CRC fails */
    do
    {
        for (idxb = 0; idxb < deviceCount; idxb++)
        {
            /* Re-initialize bus */
            if (!OW_Reset(statVar.owPinCode))
            {
                return false;
            }
            
            /* Generate ROM access code */
            romData = (*romId << 8) | DS18B20_FAMILY_CODE;
            crcData = EDC_CalculateCrc(CRC_POLY_CODE, &romData, 7);
            romData |= (crcData << 56);
            romId++;

            /* Match ROM */
            OW_WriteByte(statVar.owPinCode, MATCH_ROM_CMD);
            OW_WriteMultiByte(statVar.owPinCode, &romData, 8);

            /* Read scratch-pad */
            OW_WriteByte(statVar.owPinCode,READ_MEM_CMD);
            OW_ReadMultiByte(statVar.owPinCode, &rxData[idxb][0], 9);
            
            /* Extract sign and integer data (no floating point for alarm) */
            hiIntgr = rxData[idxb][2] & 0x7E;
            hiSign = (rxData[idxb][2] & 0x80) ? (-1) : (1);
            loIntgr = rxData[idxb][3] & 0x7E;
            loSign = (rxData[idxb][3] & 0x80) ? (-1) : (1);
            
            /* Copy HI/LO Alarm and resolution */
            *(dataBuff + idxb * 3 + 0) = (int)hiIntgr * hiSign;
            *(dataBuff + idxb * 3 + 1) = (int)loIntgr * loSign;
            *(dataBuff + idxb * 3 + 2) = (int)(rxData[idxb][4] >> 5);
                       
            /* Invalid data receive check */
            if (EDC_CalculateCrc(CRC_POLY_CODE, &rxData[idxb][0], 9) != 0)
            {
                break;
            }
        }
        
        /* All device's CRC valid */
        if (idxb == deviceCount)
        {
            break;
        }
        
        idxa++;
        
    } while (idxa < DS_READ_RAM_REPEAT_COUNT);
    
    /* Broken connection or CRC invalid and neither of devices responded */
    if ((idxa >= (DS_READ_RAM_REPEAT_COUNT - 1)) && (idxb == 0))
    {
        return false;
    }
    
    return true;
}


/******************************************************************************/
/*------------------------Local Function Definitions--------------------------*/
/******************************************************************************/


/*
 *  Generate CRC LUT one time only for active use
 */
static bool GenerateCrcLut(void)
{
    static bool isCrcLutGenerated = false;
    
    if (isCrcLutGenerated == false)
    {
        isCrcLutGenerated = true;
        
        static const CrcConfig_t crcConfig = {
            .poly = CRC_POLY_CODE,
            .polySize = CRC_POLY_SIZE,
            .isInputReflected = true,
            .isCrcReflected = true
        };
        
        if (!EDC_GenerateCrcLut(crcConfig))
        {
            return false;
        }
    }
    
    return true;
}


/*
 *  Executes ID or Alarm search
 */
static uint32_t SearchDevice(const uint32_t pinCode, uint64_t *romIdBuff, SearchMode_t searchMode)
{
    uint32_t deviceCount = 0;
    
    OwConfig_t owConfig = {
        .pinCode = pinCode,
        .speedMode = OW_STANDARD_SPEED
    };
    
    /* Pin code check */
    if (pinCode == 0)
    {
        return deviceCount;
    }
    
    /* Generate CRC LUT once for active use */
    if (!GenerateCrcLut())
    {
        return deviceCount;
    }
    
    /* Initialize OW bus + presence check */
    OW_ConfigBus(owConfig);
    if (!OW_Reset(owConfig.pinCode))
    {
        return deviceCount;
    }
    
    uint64_t romData;
    uint8_t romBit, romCmpBit, nextBit;
    int lastZero = -1;                      // -1 identifies the last device
    int lastDiscrepancy = -1;
    //int lastFamilyDiscrepancy = -1;
    bool isLastDevice = false;
    bool isResetSearch = false;
    uint8_t repeatSearchCount = 0;

    /* Determine operation type */
    uint8_t searchCmd = (searchMode == SEARCH_DEVICE_ID) ? SEARCH_ROM_CMD : ALARM_SEARCH_CMD;
    
    /* Use Core timer for timeout */
    statVar.owPinCode = owConfig.pinCode;
    statVar.sysFreq = OSC_GetSysFreq();
    uint32_t delay = DS_CONV_TEMP_TIMEOUT_MS * (statVar.sysFreq / 1000 / 2);
    uint32_t timeout = _CP0_GET_COUNT() + delay;
    
    /* Loop through all devices */
    do
    {
        lastZero = -1;
        romData = 0;
        
        /* Search ROM command */
        OW_WriteByte(owConfig.pinCode, searchCmd);
        
        /* Find ROM */
        for (uint8_t romBitIdx = 0; romBitIdx < 64; romBitIdx++)
        {
            /* Read bit and its complement */
            romBit = OW_ReadBit(owConfig.pinCode);
            romCmpBit = OW_ReadBit(owConfig.pinCode);

            /* No presence check */
            if ((romBit == romCmpBit) && (romBit == 1))
            {
                isResetSearch = true;
                break;
            }
            
            /* Case of discrepancy (devices have different current bits) */
            if ((romBit == romCmpBit) && (romBit == 0))
            {
                if (romBitIdx == lastDiscrepancy)
                {
                    nextBit = 1;
                }
                else if (romBitIdx > lastDiscrepancy)
                {
                    nextBit = 0;
                }
                /* Next bit is the same from previous ROM number */
                else
                {
                    nextBit = (*(romIdBuff + deviceCount - 1) & (1 << romBitIdx)) > 0;
                }

                if (nextBit == 0)
                {
                    lastZero = romBitIdx;
                    /*
                    if (lastZero < 9)
                    {
                        lastFamilyDiscrepancy = lastZero;
                    }
                    */
                }
            }
            /* All devices have the same current bit */
            else
            {
                nextBit = romBit;
            }

            /* Save bit and write it */
            romData |= ((uint64_t)nextBit << romBitIdx);
            OW_WriteBit(owConfig.pinCode, nextBit);
        }
        
        /* Family code check */
        if ((romData & 0xFF) == DS18B20_FAMILY_CODE)
        {
            *(romIdBuff + deviceCount) = (romData >> 8) & 0xFFFFFFFFFFFF;

            /* Verify ROM CRC */
            if ((isResetSearch == false) && (EDC_CalculateCrc(CRC_POLY_CODE, &romData, 8) == 0))
            {
                lastDiscrepancy = lastZero;

                /* End search */
                if (lastDiscrepancy == -1)
                {
                    isLastDevice = true;
                }
                /* Initialize device for next search */
                else
                {
                    if (!OW_Reset(owConfig.pinCode))
                    {
                        deviceCount = 0;
                        return deviceCount;
                    }
                }
                deviceCount++;
            }
            /* If no presence or wrong CRC */
            else
            {
                lastDiscrepancy = -1;
                //lastFamilyDiscrepancy = -1;
                isLastDevice = false;
                isResetSearch = false;
                deviceCount = 0;
                repeatSearchCount++;
            }
        }
    } while ((isLastDevice == false) &&
             (repeatSearchCount < DS_SEARCH_DEVICE_REPEAT_COUNT) &&
             (timeout > _CP0_GET_COUNT()));
    
    /* Scan not successful */
    if (isLastDevice != true)
    {
        deviceCount = 0;
    }

    return deviceCount;
}

/*
 *  Starts temperature conversion of (single/multiple) DS18B20 device
 */
static bool ConfigDevice(DsConfig_t dsConfig, bool isMultiMode)
{
    /* Single device configuration ROM check */
    if ((dsConfig.deviceId == 0) && (isMultiMode == false))
    {
        return false;
    }
    
    int hiAlarm, loAlarm;
    uint8_t rawHiAlarm, rawLoAlarm;
    
    /* Configure alarm values */
    if (hiAlarm != loAlarm)
    {
        hiAlarm = dsConfig.highAlarm + (int)statVar.tempCorr;
        loAlarm = dsConfig.lowAlarm + (int)statVar.tempCorr;    
        hiAlarm = (hiAlarm > MAX_TEMP) ? MAX_TEMP : hiAlarm;
        loAlarm = (loAlarm < MIN_TEMP) ? MIN_TEMP : loAlarm;

        /* Add negative sign if negative */
        if (hiAlarm < 0)
        {
            rawHiAlarm = 0x80 | (uint8_t)(255 - (uint8_t)hiAlarm + 1);
        }
        else
        {
            rawHiAlarm = hiAlarm;
        }
        
        /* Add negative sign if negative */
        if (loAlarm < 0)
        {
            rawLoAlarm = 0x80 | (uint8_t)(255 - (uint8_t)loAlarm + 1);
        }
        else
        {
            rawLoAlarm = loAlarm;
        }
    }
    /* Alarm flag never triggered if alarm not configured */
    else
    {
        rawHiAlarm = MAX_TEMP;
        rawLoAlarm = 0x80 | 55;
    }
    
    /* Initialize bus */
    if (!OW_Reset(statVar.owPinCode))
    {
        return false;
    }
    
    uint8_t txData[3] = {rawHiAlarm, rawLoAlarm, (dsConfig.measRes << 5)};
    
    /* Configure RAM for multiple devices */
    if (isMultiMode)
    {
        OW_WriteByte(statVar.owPinCode, SKIP_ROM_CMD);
        OW_WriteByte(statVar.owPinCode, WRITE_MEM_CMD);
        OW_WriteMultiByte(statVar.owPinCode, txData, 3);
    }
    /* Configure RAM for a single device */
    else
    {
        uint64_t romData = (dsConfig.deviceId << 8) | DS18B20_FAMILY_CODE;
        uint64_t crcData = EDC_CalculateCrc(CRC_POLY_CODE, &romData, 7);
        romData |= (crcData << 56);
    
        OW_WriteByte(statVar.owPinCode, WRITE_MEM_CMD);
        OW_WriteMultiByte(statVar.owPinCode, txData, 3);
    }
    
    return true;
}


/*
 *  Execute Copy Scratch-pad (aka. Save ROM) or Recall EEPROM (aka. Copy ROM)
 */
static bool SaveCopyRom(const uint64_t *romId, bool isMultiMode, RomMode_t romMode)
{
    /* Single device configuration ROM check */
    if ((*romId == 0) && (isMultiMode = false))
    {
        return false;
    }

    /* Initialize bus */
    if (!OW_Reset(statVar.owPinCode))
    {
        return false;
    }
    
    /* Skip ROM for multiple devices */
    if (isMultiMode == true)
    {
        OW_WriteByte(statVar.owPinCode, SKIP_ROM_CMD);
    }
    /* Access ROM for single device */
    else
    {
        uint64_t romData = (*romId << 8) | DS18B20_FAMILY_CODE;
        uint64_t crcData = EDC_CalculateCrc(CRC_POLY_CODE, &romData, 7);
        romData |= (crcData << 56);

        OW_WriteByte(statVar.owPinCode, MATCH_ROM_CMD);
        OW_WriteMultiByte(statVar.owPinCode, &romData, 8);
    }
    
    /* Save RAM settings to EEPROM */
    if (romMode == SAVE_ROM_MODE)
    {
        OW_WriteByte(statVar.owPinCode, COPY_MEM_CMD);
    }
    /* Load EEPROM settings to RAM */
    else
    {
        OW_WriteByte(statVar.owPinCode, RECALL_EEPROM_CMD);
    }
    
    /* SYSCLK default value */
    if (statVar.sysFreq == 0)
    {
        statVar.sysFreq = 8000000;
    }
    
    /* Use Core timer for timeout */
    uint32_t delay = DS_SAVE_COPY_ROM_TIMEOUT_MS * (statVar.sysFreq / 1000 / 2);
    uint32_t timeout = _CP0_GET_COUNT() + delay;

    /* Wait for conversion done */
    while (!OW_ReadBit(statVar.owPinCode) && (timeout > _CP0_GET_COUNT()));

    /* Timeout check */
    if (timeout < _CP0_GET_COUNT())
    {
        return false;
    }
    
    return true;
}