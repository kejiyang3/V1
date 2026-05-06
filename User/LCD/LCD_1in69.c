/*****************************************************************************
* | File        :   LCD_1in69.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :   Used to shield the underlying layers of each master and enhance portability
*----------------
* | This version:   V1.0
* | Date        :   2023-03-15
* | Info        :   Basic version
 *
 ******************************************************************************/
#include "LCD_1in69.h"
#include "DEV_Config.h"
extern SPI_HandleTypeDef hspi1;

#include <stdlib.h> //itoa()
#include <stdio.h>

LCD_1IN69_ATTRIBUTES LCD_1IN69;

// DMA busy flag (static to this file)
static uint8_t dmaBusyFlag = 0;

/******************************************************************************
function :  Hardware reset
parameter:
******************************************************************************/
static void LCD_1IN69_Reset(void)
{
    Debug("LCD Reset: High\r\n");
    LCD_1IN69_RST_1;
    DEV_Delay_ms(100);
    Debug("LCD Reset: Low\r\n");
    LCD_1IN69_RST_0;
    DEV_Delay_ms(100);
    Debug("LCD Reset: High\r\n");
    LCD_1IN69_RST_1;
    DEV_Delay_ms(100);
}

/******************************************************************************
function :  send command
parameter:
     Reg : Command register
******************************************************************************/
static void LCD_1IN69_SendCommand(UBYTE Reg)
{
    LCD_1IN69_DC_0;
    LCD_1IN69_CS_0;
    DEV_SPI_WRITE(Reg);
    LCD_1IN69_CS_1;
}

/******************************************************************************
function :  send data
parameter:
    Data : Write data
******************************************************************************/
static void LCD_1IN69_SendData_8Bit(UBYTE Data)
{
    LCD_1IN69_DC_1;
    LCD_1IN69_CS_0;
    DEV_SPI_WRITE(Data);
    LCD_1IN69_CS_1;
}

/******************************************************************************
function :  send data
parameter:
    Data : Write data
******************************************************************************/
static void LCD_1IN69_SendData_16Bit(UWORD Data)
{
    LCD_1IN69_DC_1;
    LCD_1IN69_CS_0;
    DEV_SPI_WRITE((Data >> 8) & 0xFF);
    DEV_SPI_WRITE(Data & 0xFF);
    LCD_1IN69_CS_1;
}

/******************************************************************************
function :  Initialize the lcd register
parameter:
******************************************************************************/
static void LCD_1IN69_InitReg(void)
{
    Debug("LCD InitReg begin\r\n");
    LCD_1IN69_SendCommand(0x11);    // Sleep Out
    DEV_Delay_ms(120);              // Mandatory delay >= 120ms

    LCD_1IN69_SendCommand(0x36);    // Memory Data Access Control
    LCD_1IN69_SendData_8Bit(0x00);

    LCD_1IN69_SendCommand(0x3A);    // Interface Pixel Format: 16bit/pixel (RGB565)
    LCD_1IN69_SendData_8Bit(0x55);

    LCD_1IN69_SendCommand(0xB2);    // Porch Setting
    LCD_1IN69_SendData_8Bit(0x0B);
    LCD_1IN69_SendData_8Bit(0x0B);
    LCD_1IN69_SendData_8Bit(0x00);
    LCD_1IN69_SendData_8Bit(0x33);
    LCD_1IN69_SendData_8Bit(0x33);

    LCD_1IN69_SendCommand(0xB7);    // Gate Control
    LCD_1IN69_SendData_8Bit(0x75);

    LCD_1IN69_SendCommand(0xBB);    // VCOM Setting
    LCD_1IN69_SendData_8Bit(0x2B);

    LCD_1IN69_SendCommand(0xC0);    // LCM Control
    LCD_1IN69_SendData_8Bit(0x2C);

    LCD_1IN69_SendCommand(0xC2);    // VDV and VRH Command Enable
    LCD_1IN69_SendData_8Bit(0x01);

    LCD_1IN69_SendCommand(0xC3);    // VRH Set
    LCD_1IN69_SendData_8Bit(0x16);

    LCD_1IN69_SendCommand(0xC4);    // VDV Set
    LCD_1IN69_SendData_8Bit(0x20);

    LCD_1IN69_SendCommand(0xC6);    // Frame Rate Control in Normal Mode
    LCD_1IN69_SendData_8Bit(0x13);

    LCD_1IN69_SendCommand(0xD0);    // Power Control 1
    LCD_1IN69_SendData_8Bit(0xA4);
    LCD_1IN69_SendData_8Bit(0xA1);

    LCD_1IN69_SendCommand(0xD6);    // NVM Control
    LCD_1IN69_SendData_8Bit(0xA1);

    LCD_1IN69_SendCommand(0xE0);    // Positive Voltage Gamma Control
    LCD_1IN69_SendData_8Bit(0xF0);
    LCD_1IN69_SendData_8Bit(0x04);
    LCD_1IN69_SendData_8Bit(0x07);
    LCD_1IN69_SendData_8Bit(0x09);
    LCD_1IN69_SendData_8Bit(0x07);
    LCD_1IN69_SendData_8Bit(0x13);
    LCD_1IN69_SendData_8Bit(0x25);
    LCD_1IN69_SendData_8Bit(0x33);
    LCD_1IN69_SendData_8Bit(0x3C);
    LCD_1IN69_SendData_8Bit(0x34);
    LCD_1IN69_SendData_8Bit(0x10);
    LCD_1IN69_SendData_8Bit(0x10);
    LCD_1IN69_SendData_8Bit(0x29);
    LCD_1IN69_SendData_8Bit(0x32);

    LCD_1IN69_SendCommand(0xE1);    // Negative Voltage Gamma Control
    LCD_1IN69_SendData_8Bit(0xF0);
    LCD_1IN69_SendData_8Bit(0x05);
    LCD_1IN69_SendData_8Bit(0x08);
    LCD_1IN69_SendData_8Bit(0x0A);
    LCD_1IN69_SendData_8Bit(0x09);
    LCD_1IN69_SendData_8Bit(0x05);
    LCD_1IN69_SendData_8Bit(0x25);
    LCD_1IN69_SendData_8Bit(0x32);
    LCD_1IN69_SendData_8Bit(0x3B);
    LCD_1IN69_SendData_8Bit(0x3B);
    LCD_1IN69_SendData_8Bit(0x17);
    LCD_1IN69_SendData_8Bit(0x18);
    LCD_1IN69_SendData_8Bit(0x2E);
    LCD_1IN69_SendData_8Bit(0x37);

    LCD_1IN69_SendCommand(0xE4);    // IPS-specific setting
    LCD_1IN69_SendData_8Bit(0x25);
    LCD_1IN69_SendData_8Bit(0x00);
    LCD_1IN69_SendData_8Bit(0x00);

    LCD_1IN69_SendCommand(0x21);    // Display Inversion On (required for IPS panel)

    LCD_1IN69_SendCommand(0x29);    // Display On
    DEV_Delay_ms(50);
}

/********************************************************************************
function:   Set the resolution and scanning method of the screen
parameter:
        Scan_dir:   Scan direction
********************************************************************************/
static void LCD_1IN69_SetAttributes(UBYTE Scan_dir)
{
    // Get the screen scan direction
    LCD_1IN69.SCAN_DIR = Scan_dir;
    UBYTE MemoryAccessReg = 0x00;

    // Get GRAM and LCD width and height
    if (Scan_dir == HORIZONTAL) {
        LCD_1IN69.HEIGHT = LCD_1IN69_WIDTH;
        LCD_1IN69.WIDTH = LCD_1IN69_HEIGHT;
        MemoryAccessReg = 0X70;
    }
    else {
        LCD_1IN69.HEIGHT = LCD_1IN69_HEIGHT;
        LCD_1IN69.WIDTH = LCD_1IN69_WIDTH;      
        MemoryAccessReg = 0X00;
    }

    // Set the read / write scan direction of the frame memory
    LCD_1IN69_SendCommand(0x36); // MX, MY, RGB mode
    LCD_1IN69_SendData_8Bit(MemoryAccessReg); // 0x08 set RGB
}

/********************************************************************************
function :  Initialize the lcd
parameter:
********************************************************************************/
void LCD_1IN69_Init(UBYTE Scan_dir)
{
    Debug("LCD_1IN69_Init begin\r\n");
    // Hardware reset
    LCD_1IN69_Reset();
    Debug("LCD reset done\r\n");

    // Set the initialization register (必须先初始化)
    LCD_1IN69_InitReg();
    Debug("LCD init registers done\r\n");

    // Set the resolution and scanning method of the screen (最后再设置方向)
    LCD_1IN69_SetAttributes(Scan_dir);
    Debug("LCD attributes set\r\n");
}

/********************************************************************************
function:   Sets the start position and size of the display area
parameter:
        Xstart  :   X direction Start coordinates
        Ystart  :   Y direction Start coordinates
        Xend    :   X direction end coordinates
        Yend    :   Y direction end coordinates
********************************************************************************/
void LCD_1IN69_SetWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
    if (LCD_1IN69.SCAN_DIR == VERTICAL) {
				// set the X coordinates
        LCD_1IN69_SendCommand(0x2A);
        LCD_1IN69_SendData_8Bit(Xstart >> 8);
        LCD_1IN69_SendData_8Bit(Xstart);
        LCD_1IN69_SendData_8Bit((Xend-1) >> 8);
        LCD_1IN69_SendData_8Bit(Xend-1);

        // set the Y coordinates
        LCD_1IN69_SendCommand(0x2B);
        LCD_1IN69_SendData_8Bit((Ystart+20) >> 8);
        LCD_1IN69_SendData_8Bit(Ystart+20);
        LCD_1IN69_SendData_8Bit((Yend+20-1) >> 8);
        LCD_1IN69_SendData_8Bit(Yend+20-1);

    }
    else {
        // set the X coordinates
        LCD_1IN69_SendCommand(0x2A);
        LCD_1IN69_SendData_8Bit((Xstart+20) >> 8);
        LCD_1IN69_SendData_8Bit(Xstart+20);
        LCD_1IN69_SendData_8Bit((Xend+20-1) >> 8);
        LCD_1IN69_SendData_8Bit(Xend+20-1);

        // set the Y coordinates
        LCD_1IN69_SendCommand(0x2B);
        LCD_1IN69_SendData_8Bit(Ystart >> 8);
        LCD_1IN69_SendData_8Bit(Ystart);
        LCD_1IN69_SendData_8Bit((Yend-1) >> 8);
        LCD_1IN69_SendData_8Bit(Yend-1);
    }
    
    LCD_1IN69_SendCommand(0X2C);
}

/******************************************************************************
function :  Clear screen
parameter:
******************************************************************************/
void LCD_1IN69_Clear(UWORD Color)
{
    UWORD i,j;
    
    LCD_1IN69_SetWindows(0, 0, LCD_1IN69.WIDTH, LCD_1IN69.HEIGHT);
    DEV_Digital_Write(DEV_DC_PIN, 1);
    
    LCD_1IN69_CS_0; // ！！这里必须拉低片选！！
    
    for(i = 0; i < LCD_1IN69_WIDTH; i++) {
        for(j = 0; j < LCD_1IN69_HEIGHT; j++) {
            DEV_SPI_WRITE((Color>>8)&0xff);
            DEV_SPI_WRITE(Color);
        }
    }
    
    LCD_1IN69_CS_1; // ！！循环结束后必须拉高片选！！
}

/******************************************************************************
function :  Sends the image buffer in RAM to displays
parameter:
******************************************************************************/
void LCD_1IN69_Display(UWORD *Image)
{
    UWORD i,j;

    LCD_1IN69_SetWindows(0, 0, LCD_1IN69.WIDTH, LCD_1IN69.HEIGHT);
    DEV_Digital_Write(DEV_DC_PIN, 1);
    
    LCD_1IN69_CS_0; // ！！新增！！
    
    for(i = 0; i < LCD_1IN69_WIDTH; i++) {
        for(j = 0; j < LCD_1IN69_HEIGHT; j++) {
            DEV_SPI_WRITE((*(Image+i*LCD_1IN69_HEIGHT+j)>>8)&0xff);
            DEV_SPI_WRITE(*(Image+i*LCD_1IN69_WIDTH+j));
        }
    }
    
    LCD_1IN69_CS_1; // ！！新增！！
}
void LCD_1IN69_DisplayWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD *Image)
{
    // display
    UDOUBLE Addr = 0;
    UWORD i,j;
    
    LCD_1IN69_SetWindows(Xstart, Ystart, Xend , Yend);
    LCD_1IN69_DC_1;
    
    LCD_1IN69_CS_0; // ！！新增！！
    
    for (i = Ystart; i < Yend; i++) {
        Addr = Xstart + i * LCD_1IN69_WIDTH ;
        for(j=Xstart;j<Xend;j++) {
            DEV_SPI_WRITE((*(Image+Addr+j)>>8)&0xff);
            DEV_SPI_WRITE(*(Image+Addr+j));
        }
    }
    
    LCD_1IN69_CS_1; // ！！新增！！
}

/******************************************************************************
function :  Draw Area (Specially for LVGL partial buffer flush)
parameter:
    Xstart  :   X direction Start coordinates
    Ystart  :   Y direction Start coordinates
    Xend    :   X direction end coordinates (exclusive)
    Yend    :   Y direction end coordinates (exclusive)
    Image   :   Pointer to the partial buffer (index starts from 0)
******************************************************************************/
void LCD_1IN69_DrawArea(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD *Image)
{
    uint32_t size = (Xend - Xstart) * (Yend - Ystart);
    uint8_t *pData = (uint8_t *)Image;

    LCD_1IN69_SetWindows(Xstart, Ystart, Xend, Yend);
    LCD_1IN69_DC_1;
    LCD_1IN69_CS_0;

    // 🌟 使用 DMA 直接搬运 8 位像素流（因为是 16位 颜色，所以数据量是 size * 2）
    HAL_SPI_Transmit_DMA(&hspi1, pData, size * 2);

    // ⚠️ 注意：这里绝对不能拉高 CS (LCD_1IN69_CS_1)！
    // 此时函数会立刻返回，但后台 DMA 还在发数据。
}

void LCD_1IN69_DrawPoint(UWORD X, UWORD Y, UWORD Color)
{
    LCD_1IN69_SetWindows(X, Y, X, Y);
    LCD_1IN69_SendData_16Bit(Color);
}

void LCD_1IN69_SetBackLight(UWORD Value)
{
    DEV_Set_PWM(Value);
}

/******************************************************************************
function :  Display window using DMA (non-blocking)
parameter:
    Xstart  :   X direction Start coordinates
    Ystart  :   Y direction Start coordinates
    Xend    :   X direction end coordinates
    Yend    :   Y direction end coordinates
    Image   :   Image buffer (continuous window data, RGB565 format)
******************************************************************************/
void LCD_1IN69_DisplayWindows_DMA(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD *Image)
{
    // Calculate window size
    UWORD width = Xend - Xstart;
    UWORD height = Yend - Ystart;
    UDOUBLE pixelCount = width * height;
    UDOUBLE byteCount = pixelCount * 2;  // Each pixel is 2 bytes

    // Safety check: DMA maximum transfer size is 65535 bytes
    if (byteCount > 65535) {
        Debug("Error: DMA transfer size %lu exceeds 65535 limit\r\n", byteCount);
        return;
    }

    // Limit maximum buffer size to 32KB for stack safety
    #define MAX_DMA_BUFFER_SIZE 32000
    if (byteCount > MAX_DMA_BUFFER_SIZE) {
        Debug("Error: DMA transfer size %lu exceeds buffer limit %d\r\n", byteCount, MAX_DMA_BUFFER_SIZE);
        return;
    }

    // Reentrancy protection (simple busy flag)
    if (dmaBusyFlag) {
        Debug("Error: DMA transfer already in progress\r\n");
        return;
    }
    dmaBusyFlag = 1;

    // Set display window
    LCD_1IN69_SetWindows(Xstart, Ystart, Xend, Yend);

    // Prepare for data transfer: CS low, DC high
    LCD_1IN69_CS_0;
    LCD_1IN69_DC_1;

    // Convert UWORD array to byte array (MSB first for each pixel)
    // Using static buffer that persists during DMA transfer
    static uint8_t dmaBuffer[MAX_DMA_BUFFER_SIZE];
    uint32_t i;

    for (i = 0; i < pixelCount; i++) {
        dmaBuffer[i * 2] = (Image[i] >> 8) & 0xFF;  // High byte
        dmaBuffer[i * 2 + 1] = Image[i] & 0xFF;     // Low byte
    }

    // Start DMA transfer
    HAL_StatusTypeDef status = HAL_SPI_Transmit_DMA(&hspi1, dmaBuffer, byteCount);
    if (status != HAL_OK) {
        Debug("Error: HAL_SPI_Transmit_DMA failed with status %d\r\n", status);
        dmaBusyFlag = 0;  // Reset busy flag on error
        LCD_1IN69_CS_1;  // Raise CS on error
    }
    // On success, busy flag will be cleared in HAL_SPI_TxCpltCallback
}

/******************************************************************************
function :  Check if DMA transfer is in progress
parameter:
    returns 1 if busy, 0 if idle
******************************************************************************/
uint8_t LCD_1IN69_DMAIsBusy(void)
{
    return dmaBusyFlag;
}

/******************************************************************************
function :  Clear DMA busy flag (call from SPI callback)
parameter:
******************************************************************************/
void LCD_1IN69_DMAClearBusy(void)
{
    dmaBusyFlag = 0;
}
