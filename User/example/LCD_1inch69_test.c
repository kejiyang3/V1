#include "image.h"
#include "LCD_Test.h"
#include "LCD_1in69.h"
#include "DEV_Config.h"


void LCD_1in69_test()
{
    printf("LCD_1IN69 Simple Text Demo\r\n");
    DEV_Module_Init();

    printf("LCD_1IN69_ Init and Clear...\r\n");
    LCD_1IN69_SetBackLight(1000);
    LCD_1IN69_Init(VERTICAL);

    // Test screen with solid colors first
    printf("Testing with solid colors...\r\n");
    LCD_1IN69_Clear(RED);
    DEV_Delay_ms(500);
    LCD_1IN69_Clear(GREEN);
    DEV_Delay_ms(500);
    LCD_1IN69_Clear(BLUE);
    DEV_Delay_ms(500);
    LCD_1IN69_Clear(WHITE);
    DEV_Delay_ms(500);

    printf("Paint_NewImage\r\n");
    Paint_NewImage(LCD_1IN69_WIDTH, LCD_1IN69_HEIGHT, 0, WHITE);

    printf("Set Clear and Display Funtion\r\n");
    Paint_SetClearFuntion(LCD_1IN69_Clear);
    Paint_SetDisplayFuntion(LCD_1IN69_DrawPoint);

    printf("Paint_Clear\r\n");
    Paint_Clear(WHITE);
    DEV_Delay_ms(100);

    printf("Displaying simple text...\r\n");

    // Display simple static text
    Paint_DrawString_EN(20, 20, "STM32L496", &Font24, BLACK, WHITE);
    Paint_DrawString_EN(20, 50, "LCD Test", &Font24, BLUE, WHITE);
    Paint_DrawString_EN(20, 80, "Simple Display", &Font16, RED, WHITE);
    Paint_DrawString_EN(20, 100, "Hello World!", &Font16, GREEN, WHITE);

    // Draw a simple border
    Paint_DrawRectangle(10, 10, LCD_1IN69_WIDTH-10, LCD_1IN69_HEIGHT-10, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

    printf("Display complete. Screen will stay on.\r\n");
}

