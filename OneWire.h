#ifndef ONEWIRE_H
#define	ONEWIRE_H

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

/** Standard libs **/
#include <stdio.h>

/** Custom libs **/
#include "Sfr_types.h"
#include "Pio.h"
#include "Tmr.h"

/******************************************************************************/
/*---------------------------------Macros-------------------------------------*/
/******************************************************************************/



/******************************************************************************/
/*----------------------------Enumeration Types-------------------------------*/
/******************************************************************************/

/* OW speed flags */
typedef enum {
    OW_STANDARD_SPEED = 0,  
    OW_HIGH_SPEED = 1,
    OW_OVERLOAD_SPEED = 2
} OwSpeedMode_t;


/******************************************************************************/
/*-----------------------------Data Structures--------------------------------*/
/******************************************************************************/

/* OW protocol configuration structure */
typedef struct {
    const uint32_t  pinCode;
    OwSpeedMode_t   speedMode;
} OwConfig_t;

/******************************************************************************/
/*---------------------------- Function Prototypes----------------------------*/
/******************************************************************************/

bool OW_ConfigBus(OwConfig_t owConfig);
void OW_ConfigSpeedMode(OwSpeedMode_t speedMode);
bool OW_Reset(const uint32_t pinCode);
void OW_WriteBit(const uint32_t pinCode, const uint8_t dataBit);
uint8_t OW_ReadBit(const uint32_t pinCode);
void OW_WriteByte(const uint32_t pinCode, uint8_t dataByte);
void OW_ReadByte(const uint32_t pinCode, void *dataPtr);
void OW_WriteMultiByte(const uint32_t pinCode, void *dataPtr, uint8_t dataLen);
void OW_ReadMultiByte(const uint32_t pinCode, void *dataPtr, uint8_t dataLen);


#endif	/* ONEWIRE_H */

