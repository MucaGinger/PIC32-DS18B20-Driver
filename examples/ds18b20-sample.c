/** Required by OneWire.h for Tmr.h **/
#define TMR_DELAY_SYSCLK 40000000

/** Custom libs **/
#include "DS18B20.h"

int main (int argc, char** argv)
{
    /* Pin toggle during timeout */
    PIO_ClearPin(GPIO_RPB4);
    PIO_ConfigGpioPin(GPIO_RPB4, PIO_TYPE_DIGITAL, PIO_DIR_OUTPUT);
    
    OwConfig_t owConfigBus = 
    {
        .pinCode = GPIO_RPB5,
        .speedMode = OW_STANDARD_SPEED
    };
    
    /* Configuration structure for multiple devices */
    DsConfig_t dsConfig = {
        .measRes = DS_MEAS_RES_12BIT,
        .owConfig = owConfigBus,
        .highAlarm = 40,
        .lowAlarm = 24
    };
    
    /* Identify all DS18B20 devices */
    uint64_t romId[10] = {0};
    uint32_t deviceCount;
    deviceCount = DS18B20_SearchDeviceId(owConfigBus.pinCode, romId);
    
    /* Check if any are fake - have fixed conversion time */
    for (uint8_t idx = 0; idx < deviceCount; idx++)
    {
        if (DS18B20_IsDeviceFake(&romId[idx]))
        {
            dsConfig.measRes = DS_MEAS_RES_12BIT;
            break;
        }
    }
    
    /* Multiple devices on OW bus check */
    bool isMultiMode;
    isMultiMode = (deviceCount > 1) ? true : false;
    
    float data[deviceCount];
    
    /* Configure device */
    if (DS18B20_ConfigDevice(dsConfig, isMultiMode))
    {
        /* Store alarm settings */
        DS18B20_SaveToRom(romId, isMultiMode);
        
        /* Start temperature conversion with internal timeout */
        DS18B20_ConvertTemp(romId, deviceCount);
        
        /* Wait for timeout */
        while (!DS18B20_IsConvDone())
        {
            PIO_TogglePin(GPIO_RPB4);
        }
        
        /* Store results */
        DS18B20_ReadTemp(romId, data, deviceCount);
    }
    else
    {
        /* Do not proceed until device successfully configured */
    }
    
    /* Do alarm flag search */
    uint64_t alarmRomId[10];
    uint32_t alarmCount;
    alarmCount = DS18B20_SearchAlarm(owConfigBus.pinCode, alarmRomId);
    
    /* Devices at 25-40°C won't have their alarm flags set */
    
    /* Reconfigure to another alarm setting */
    dsConfig.lowAlarm = 0;
    dsConfig.highAlarm = 15;
    DS18B20_ConfigDevice(dsConfig, isMultiMode);

    /* Do another conversion */
    DS18B20_ConvertReadTemp(romId, data, deviceCount);

    /* Do another alarm search */
    alarmCount = DS18B20_SearchAlarm(owConfigBus.pinCode, alarmRomId);
    
    /* Alarm should be now triggered for devices above 15°C */
    
    /* Restore alarm settings */
    DS18B20_CopyFromRom(romId, isMultiMode);
    
    /* Check one of devices' EEPROM if successfully copied */
    int ramData[3];
    DS18B20_ReadRam(romId, ramData, 1);
    
    /* Do third conversion */
    DS18B20_ConvertReadTemp(romId, data, deviceCount);

    /* Do third alarm search */
    alarmCount = DS18B20_SearchAlarm(owConfigBus.pinCode, alarmRomId);
    
    /* This time devices at 25-40°C won't have their alarm flags set */

    /* Example code end */
    while (1)
    {}
    
    return 0;
}