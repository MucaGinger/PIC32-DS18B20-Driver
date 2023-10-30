#include "Edc.h"

/* Do not change */
#define CRC_MSG_SIZE    8       // CRC is usually processed over 8-bit bytes
#define CRC_LUT_SIZE    256     // Max 2^32 sized LUT

/******************************************************************************/
/*--------------------------Local Data Variables------------------------------*/
/******************************************************************************/

/* "crcLutId" holds polynomials and "crcLut" holds LUT for corresponding index */
/* Additionally, former stores info about other CRC parameters */
static uint32_t crcLutId[CRC_MAX_DEVICE_COUNT][4];
static uint32_t crcLut[CRC_MAX_DEVICE_COUNT][CRC_LUT_SIZE];

/* LUT for bit swapping */
static uint8_t bitSwapLut[16] = { 0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
                                  0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};

/******************************************************************************/
/*--------------------------Local Data Structures-----------------------------*/
/******************************************************************************/



/******************************************************************************/
/*------------------------Local Function Prototypes---------------------------*/
/******************************************************************************/

static INLINE uint8_t BitSwap8(uint8_t byte);
static INLINE uint16_t BitSwap16(uint16_t word);
static INLINE uint32_t BitSwap32(uint32_t dword);

/******************************************************************************/
/*----------------------External Function Definitions-------------------------*/
/******************************************************************************/

/*
 *  Generate LUT for the selected polynomial of CRCn
 *  WARNING: There is some issue in generating LUT data for CRC32, where
 *  polynomial has more than 6 hexadecimal digits (24 bytes)
 */
extern bool EDC_GenerateCrcLut(CrcConfig_t crcConfig)
{
    uint8_t poly = crcConfig.poly;
    uint8_t polySize = crcConfig.polySize;
    uint8_t isInputRefl = crcConfig.isInputReflected;
    uint8_t isCrcRefl = crcConfig.isCrcReflected;
    
    /* Poly check */
    if (poly == 0)
    {
        return false;
    }
    
    /* CRC size check */
    if ((polySize != 8) && (polySize != 16) && (polySize != 32) )
    {
        return false;
    }
    
    /* The following code determines where to generate LUT in given array */
    
    /* Check if duplicate polynomial value exists */
    for (uint8_t idx = 0; idx < CRC_MAX_DEVICE_COUNT; idx++)
    {
        if (poly == crcLutId[idx][0])
        {
            return false;
        }
    }
    
    static uint8_t deviceIdx = 0;
    /* Max amount of devices check */
    if (deviceIdx >= CRC_MAX_DEVICE_COUNT)
    {
        return false;
    }
    
    /* These parameters are accessed when CRC is calculated (based on poly) */
    crcLutId[deviceIdx][0] = poly;
    crcLutId[deviceIdx][1] = polySize;
    crcLutId[deviceIdx][2] = isInputRefl;
    crcLutId[deviceIdx][3] = isCrcRefl;
    
    /* The following code generates CRC LUT */
    
    uint64_t alignMask, polyMask;
    uint32_t crcVal;
    
    /* Generate CRC for each of possible input - LUT */
    for (uint32_t crcIdx = 0; crcIdx < CRC_LUT_SIZE; crcIdx++)
    {
        /* Initial partial CRC value (adding zeros to the right side) */
        crcVal = crcIdx << polySize;
        /* Adding zeros to polynomial's right side */
        polyMask = poly << (CRC_MSG_SIZE - 1);
        /* Starting point for XORing individual bits of poly-sized bit chunk */
        alignMask = 1 << (CRC_MSG_SIZE + polySize - 1);
        
        /* Loop through all bits of each partial remainder */
        for (uint8_t bitIdx = 0; bitIdx < CRC_MSG_SIZE; bitIdx++)
        {
            /* XOR when "polyMask" and current partial remainder's MSB align */
            if (crcVal & alignMask)
            {
                crcVal ^= polyMask;
                crcVal ^= alignMask;
            }
            
            polyMask >>= 1;
            alignMask >>= 1;
        }
        
        /* CRC of current LUT index is stored in LUT at current index */
        crcLut[deviceIdx][crcIdx] = crcVal;
    }
    
    /* Next CRC LUT generated at next index */
    deviceIdx++;
    
    return true;
}


/*
 *  Calculate CRC of given data bytes (if CRC of whole packet is put at the end
 *  of the packet then valid data may was processed if function returns 0x00)
 * 
 *  Returns all ones if any input restriction triggered, while all zeros is
 *  considered a valid return (when comparing CRC code with valid CRC'ed message)
 */
extern uint32_t EDC_CalculateCrc(const uint32_t poly, void *dPtr, const uint32_t dataLen)
{    
    /* Input check */
    if ((dataLen == 0) || (dPtr == NULL))
    {
        return 0xFFFFFFFF;  // 0x00 reserved for valid CRC processed value
    }
    
    /* Find LUT index in "crcLut" for current polynomial */
    uint8_t lutIdx;
    for (uint8_t idx = 0; idx < CRC_MAX_DEVICE_COUNT; idx++)
    {
        if (crcLutId[idx][0] == poly)
        {
            lutIdx = idx;
            break;
        }
    }
    
    uint8_t *dataPtr;
    CrcPolySize_t polySize = crcLutId[lutIdx][1];
    bool isInputRefl = crcLutId[lutIdx][2];
    bool isCrcRefl = crcLutId[lutIdx][3];

    /* Reflect all input bytes */
    uint8_t data[dataLen];
    if (isInputRefl == true)
    {
        for (uint32_t idx = 0; idx < dataLen; idx++)
        {
            data[idx] = BitSwap8(*((uint8_t *)dPtr + idx));
        }
        dataPtr = &data[0];
    }
    else
    {
        dataPtr = dPtr;
    }
    
    uint64_t crcIdx, crcVal = 0;
    
    /* XOR all elements of input data */
    if (polySize == CRC_POLY_SIZE_8)
    {
        for (uint32_t idx = 0; idx < dataLen; idx++)
        {
            crcIdx = crcVal ^ *dataPtr;
            crcVal = crcLut[lutIdx][crcIdx];
            crcVal &= 0xFF;
            dataPtr++;
        }
        
        /* Reflect result */
        if (isCrcRefl == true)
        {
            crcVal = BitSwap8(crcVal);
        }
    }
    else if (polySize == CRC_POLY_SIZE_16)
    {
        for (uint32_t idx = 0; idx < dataLen; idx++)
        {
            crcIdx = (crcVal >> 8) ^ *dataPtr;
            crcVal = (crcVal << 8) ^ crcLut[lutIdx][crcIdx];
            crcVal &= 0xFFFF;
            dataPtr++;
        }
        
        /* Reflect result */
        if (isCrcRefl == true)
        {
            crcVal = BitSwap16(crcVal);
        }
    }
    else if (polySize == CRC_POLY_SIZE_32)
    {
        for (uint32_t idx = 0; idx < dataLen; idx++)
        {
            crcIdx = (crcVal >> 24) ^ *dataPtr;
            crcVal = (crcVal << 8) ^ crcLut[lutIdx][crcIdx];
            crcVal &= 0xFFFFFFFF;
            dataPtr++;
        }
        
        /* Reflect result */
        if (isCrcRefl == true)
        {
            crcVal = BitSwap32(crcVal);
        }
    }
    else
    {
        return 0xFFFFFFFF;  // 0x00 reserved for valid CRC processed value
    }
    
    return (uint32_t)crcVal;
}

/******************************************************************************/
/*------------------------Local Function Definitions--------------------------*/
/******************************************************************************/

/*
 *  Optimized bit reflection up to 8-bits
 */
static INLINE uint8_t BitSwap8(uint8_t byte)
{
    return (bitSwapLut[byte & 0xF] << 4) | bitSwapLut[byte >> 4];
}


/*
 *  Optimized bit reflection up to 16-bits
 */
static INLINE uint16_t BitSwap16(uint16_t word)
{
    uint8_t n1 = (word >> 0) & 0xFF;
    uint8_t n2 = (word >> 8) & 0xFF;
    uint8_t res1 = (bitSwapLut[n1 & 0xF] << 4) | bitSwapLut[n1 >> 4];
    uint8_t res2 = (bitSwapLut[n2 & 0xF] << 4) | bitSwapLut[n2 >> 4];
	
    return (res1 << 8) | (res2 << 0);
}


/*
 *  Optimized bit reflection up to 32-bits
 */
static INLINE uint32_t BitSwap32(uint32_t dword)
{
    uint8_t n1 = (dword >> 0) & 0xFF;
    uint8_t n2 = (dword >> 8) & 0xFF;
    uint8_t n3 = (dword >> 16) & 0xFF;
    uint8_t n4 = (dword >> 24) & 0xFF;
    uint8_t res1 = (bitSwapLut[n1 & 0xF] << 4) | bitSwapLut[n1 >> 4];
    uint8_t res2 = (bitSwapLut[n2 & 0xF] << 4) | bitSwapLut[n2 >> 4];
    uint8_t res3 = (bitSwapLut[n3 & 0xF] << 4) | bitSwapLut[n3 >> 4];
    uint8_t res4 = (bitSwapLut[n4 & 0xF] << 4) | bitSwapLut[n4 >> 4];
	
    return (res1 << 24) | (res2 << 16) | (res3 << 8) | (res4 << 0);
}