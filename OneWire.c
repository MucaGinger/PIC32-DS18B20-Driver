#include "OneWire.h"

/******************************************************************************/
/*--------------------------Local Data Variables------------------------------*/
/******************************************************************************/



/******************************************************************************/
/*--------------------------Local Data Structures-----------------------------*/
/******************************************************************************/

/** Protocol delays for speed mode control **/
static struct {
    uint16_t a;
    uint16_t b;
    uint16_t c;
    uint16_t d;
    uint16_t e;
    uint16_t f;
    uint16_t g;
    uint16_t h;
    uint16_t i;
    uint16_t j;
} owDelay;

/******************************************************************************/
/*------------------------Local Function Prototypes---------------------------*/
/******************************************************************************/

/** Basic OW protocol functions **/
static INLINE void SetBit(const uint32_t pinCode);
static INLINE void ClearBit(const uint32_t pinCode);
static INLINE uint8_t ReadBit(const uint32_t pinCode);
static INLINE uint8_t Reset(const uint32_t pinCode);

/******************************************************************************/
/*----------------------External Function Definitions-------------------------*/
/******************************************************************************/

/*
 *  Configure OW bus for operation (requires delay configuration)
 */
extern bool OW_ConfigBus(OwConfig_t owConfig)
{
    /* Idle state HIGH (external pull-up required due to direction change) */
    PIO_ConfigGpioPin(owConfig.pinCode, PIO_TYPE_DIGITAL, PIO_DIR_INPUT);
    
    /* Configure speed mode */
    OW_ConfigSpeedMode(owConfig.speedMode);
    
    return true;
}


/*
 *  Configure speed mode of OW communication
 */
extern void OW_ConfigSpeedMode(OwSpeedMode_t speedMode)
{
    switch(speedMode)
    {
        case OW_OVERLOAD_SPEED:
            owDelay.a = 2;
            owDelay.b = 8;
            owDelay.c = 8;
            owDelay.d = 3;
            owDelay.e = 1;
            owDelay.f = 7;
            owDelay.g = 3;
            owDelay.h = 70;
            owDelay.i = 8;
            owDelay.j = 40;
            break;
        /* This is unofficial mode */
        case OW_HIGH_SPEED:
            owDelay.a = 6;
            owDelay.b = 35;
            owDelay.c = 40;
            owDelay.d = 5;
            owDelay.e = 8;
            owDelay.f = 25;
            owDelay.g = 0;
            owDelay.h = 300;
            owDelay.i = 70;
            owDelay.j = 120;
            break;
        case OW_STANDARD_SPEED:
        default:
            owDelay.a = 6;
            owDelay.b = 64;
            owDelay.c = 60;
            owDelay.d = 10;
            owDelay.e = 9;
            owDelay.f = 55;
            owDelay.g = 0;
            owDelay.h = 480;
            owDelay.i = 70;
            owDelay.j = 410;
            break;
    }
}


/*
 *  Reset the OW bus and return presence detected
 */
extern bool OW_Reset(const uint32_t pinCode)
{
    return !Reset(pinCode);
}


/*
 *  Write one bit (used primarily for ROM search)
 */
extern void OW_WriteBit(const uint32_t pinCode, const uint8_t dataBit)
{
    /* Obtain old interrupt status and disable interrupts */
    uint32_t intrStatus = IC_GetInterruptState();
    IC_DisableInterrupts();
    
    (dataBit & 0x01) ? SetBit(pinCode) : ClearBit(pinCode);
    
    /* Restore interrupt state */
    IC_SetInterruptState(intrStatus);
}

/*
 *  Read one bit (used primarily for conversion done check or ROM search)
 */
extern uint8_t OW_ReadBit(const uint32_t pinCode)
{
    return ReadBit(pinCode);
}


/*
 *  Send one byte on the OW bus
 */
extern void OW_WriteByte(const uint32_t pinCode, uint8_t dataByte)
{
    /* Obtain old interrupt status and disable interrupts */
    uint32_t intrStatus = IC_GetInterruptState();
    IC_DisableInterrupts();
    
    for (uint8_t idx = 0; idx < 8; idx++)
    {
        ((dataByte >> idx) & 0x01) ? SetBit(pinCode) : ClearBit(pinCode);   // LSB first
    }
    
    /* Restore interrupt state */
    IC_SetInterruptState(intrStatus);
}


/*
 *  Send more bytes on the OW bus
 */
extern void OW_WriteMultiByte(const uint32_t pinCode, void *dataPtr, uint8_t dataLen)
{
    uint8_t *dataByte = dataPtr;
    
    /* Obtain old interrupt status and disable interrupts */
    uint32_t intrStatus = IC_GetInterruptState();
    IC_DisableInterrupts();
    
    /* Send each byte */
    while (dataLen--)
    {
        for (uint8_t idx = 0; idx < 8; idx++)
        {
            ((*dataByte >> idx) & 0x01) ? SetBit(pinCode) : ClearBit(pinCode);   // LSB first
        }
        dataByte++;
    }
    
    /* Restore interrupt state */
    IC_SetInterruptState(intrStatus);
}


/*
 *  Read one byte on the OW bus
 */
extern void OW_ReadByte(const uint32_t pinCode, void *dataPtr)
{
    uint8_t *dataByte = dataPtr;
    
    /* Obtain old interrupt status and disable interrupts */
    uint32_t intrStatus = IC_GetInterruptState();
    IC_DisableInterrupts();
    
    /* Skip if pointer not initialized */
    if (dataByte != NULL)
    {
        *dataByte = 0x00;
        for (uint8_t idx = 0; idx < 8; idx++)
        {
            *dataByte |= (ReadBit(pinCode) << idx);   // LSB first
        }
    }
    
    /* Restore interrupt state */
    IC_SetInterruptState(intrStatus);
}


/*
 *  Read more bytes on the OW bus
 */
extern void OW_ReadMultiByte(const uint32_t pinCode, void *dataPtr, uint8_t dataLen)
{
    uint8_t *dataByte = dataPtr;
    
    /* Obtain old interrupt status and disable interrupts */
    uint32_t intrStatus = IC_GetInterruptState();
    IC_DisableInterrupts();
    
    /* Skip if pointer not initialized */
    if (dataByte != NULL)
    {
        while (dataLen--)
        {
            *dataByte = 0x00;
            for (uint8_t idx = 0; idx < 8; idx++)
            {
                *dataByte |= (ReadBit(pinCode) << idx);   // LSB first
            }
            dataByte++;
        }
    }
    
    /* Restore interrupt state */
    IC_SetInterruptState(intrStatus);
}

/******************************************************************************/
/*------------------------Local Function Definitions--------------------------*/
/******************************************************************************/

/*
 *  Generate a single HIGH state on the OW bus
 */
static INLINE void SetBit(const uint32_t pinCode)
{
    PIO_ClearPin(pinCode);
    PIO_ConfigGpioPinDir(pinCode, PIO_DIR_OUTPUT);
    TMR_DelayUs(owDelay.a);
    PIO_ConfigGpioPinDir(pinCode, PIO_DIR_INPUT);
    TMR_DelayUs(owDelay.b);
}


/*
 *  Generate a single LOW state on the OW bus
 */
static INLINE void ClearBit(const uint32_t pinCode)
{
    PIO_ClearPin(pinCode);
    PIO_ConfigGpioPinDir(pinCode, PIO_DIR_OUTPUT);
    TMR_DelayUs(owDelay.c);
    PIO_ConfigGpioPinDir(pinCode, PIO_DIR_INPUT);
    TMR_DelayUs(owDelay.d);
}


/*
 *  Read a single bit on the OW bus
 */
static INLINE uint8_t ReadBit(const uint32_t pinCode)
{
    PIO_ClearPin(pinCode);
    PIO_ConfigGpioPinDir(pinCode, PIO_DIR_OUTPUT);
    TMR_DelayUs(owDelay.a);
    PIO_ConfigGpioPinDir(pinCode, PIO_DIR_INPUT);
    TMR_DelayUs(owDelay.e);
    uint8_t bitVal = PIO_ReadPin(pinCode);
    TMR_DelayUs(owDelay.f);
    
    return bitVal;
}


/*
 *  Generate a reset sequence on the OW bus
 */
static INLINE uint8_t Reset(const uint32_t pinCode)
{
    /* Obtain old interrupt status and disable interrupts */
    uint32_t intrStatus = IC_GetInterruptState();
    IC_DisableInterrupts();
    
    PIO_ClearPin(pinCode);
    PIO_ConfigGpioPinDir(pinCode, PIO_DIR_OUTPUT);
    TMR_DelayUs(owDelay.h);
    PIO_ConfigGpioPinDir(pinCode, PIO_DIR_INPUT);
    TMR_DelayUs(owDelay.i);
    uint8_t bitVal = PIO_ReadPin(pinCode);
    TMR_DelayUs(owDelay.j);
    
    /* Restore interrupt state */
    IC_SetInterruptState(intrStatus);
    
    return bitVal;
}