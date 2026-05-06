/**
 * @file lv_port_disp.c
 * @brief LVGL display driver porting implementation (first-stage bring-up)
 * @note Stage 1: Single partial buffer + blocking transfer
 *       No DMA, no double buffering, no performance optimization
 */

#include "lv_port_disp.h"
#include "lvgl.h"
#include "LCD_1in69.h"
#include "DEV_Config.h"
#include <stdint.h>

/* External SPI handle (defined in main.c) */
extern SPI_HandleTypeDef hspi1;

/*-------------------------------------------
 *  Configuration
 *------------------------------------------*/

/**
 * @brief Height of the partial buffer in lines
 *
 * Using 20 lines (240×20 = 4800 pixels = 9.6KB)
 * This is a trade-off between memory usage and DMA transfer frequency.
 */
#ifndef LV_PORT_DISP_BUF_HEIGHT
#define LV_PORT_DISP_BUF_HEIGHT 20
#endif

/**
 * @brief Size of the partial buffer in pixels
 *
 * Buffer holds LV_PORT_DISP_BUF_HEIGHT lines of 240 pixels each.
 */
#define LV_PORT_DISP_BUF_SIZE (240 * LV_PORT_DISP_BUF_HEIGHT)

/*-------------------------------------------
 *  Static variables
 *------------------------------------------*/

/** Single partial buffer (not double-buffered in this stage) */
static lv_color_t disp_buf[LV_PORT_DISP_BUF_SIZE];

/** LVGL draw buffer descriptor */
static lv_disp_draw_buf_t draw_buf;

/** LVGL display driver descriptor */
static lv_disp_drv_t disp_drv;

/** Active display driver for DMA completion callback */
static lv_disp_drv_t* active_disp_drv = NULL;

/*-------------------------------------------
 *  Static functions
 *------------------------------------------*/

/**
 * @brief Flush callback for LVGL display driver
 *
 * Converts LVGL's rendering buffer to LCD physical coordinates and
 * transfers via blocking SPI (LCD_1IN69_DisplayWindows).
 *
 * @note LVGL passes inclusive coordinates (x1,y1,x2,y2).
 *       LCD driver expects exclusive end coordinates (x2,y2 not included).
 *       Y-offset (+20) is handled inside LCD_1IN69_SetWindows().
 *
 * @param disp_drv Pointer to LVGL display driver
 * @param area     Area to flush (inclusive coordinates)
 * @param color_p  Pixel data buffer (RGB565 format)
 */
static void lv_port_disp_flush(lv_disp_drv_t *disp_drv,
                               const lv_area_t *area,
                               lv_color_t *color_p)
{
    /* Parameter validation */
    if (!area || !color_p) {
        return;
    }

    /* Save active display driver for DMA completion callback */
    active_disp_drv = disp_drv;

    /* Debug output (can be removed later) */
    // usb_printf("[LVGL] Flush area: (%d,%d)-(%d,%d)\r\n",
    //            area->x1, area->y1, area->x2, area->y2);

    /*
     * Coordinate conversion:
     * 1. LVGL uses inclusive coordinates (x2,y2 are inside the area)
     * 2. LCD driver expects exclusive end coordinates (x2,y2 not included)
     * 3. Y-offset (+20) is handled inside LCD_1IN69_SetWindows()
     *
     * Conversion: exclusive_end = inclusive_end + 1
     */
    UWORD x_start = (UWORD)area->x1;
    UWORD y_start = (UWORD)area->y1;
    UWORD x_end   = (UWORD)area->x2 + 1;   // Convert to exclusive
    UWORD y_end   = (UWORD)area->y2 + 1;   // Convert to exclusive

    /*
     * Type conversion: lv_color_t* → UWORD*
     * Both are 16-bit RGB565, so simple cast is safe.
     */
    UWORD *image_data = (UWORD *)color_p;

    /*
     * DMA asynchronous transfer using LVGL-optimized driver
     * LCD_1IN69_DrawArea() handles:
     * - Window setup via LCD_1IN69_SetWindows()
     * - Y-offset (+20) internally
     * - SPI DMA transfer with correct partial buffer indexing
     */
    LCD_1IN69_DrawArea(x_start, y_start, x_end, y_end, image_data);

    /*
     * IMPORTANT: DO NOT notify LVGL that flush is complete here!
     * DMA transfer is in progress, notification will happen in
     * HAL_SPI_TxCpltCallback when transfer completes.
     */
    // lv_disp_flush_ready(disp_drv);
}

/*-------------------------------------------
 *  Public functions
 *------------------------------------------*/

/**
 * @brief Initialize LVGL display driver
 *
 * This function:
 * 1. Initializes draw buffer with single partial buffer
 * 2. Configures display driver parameters
 * 3. Registers flush callback
 * 4. Registers driver with LVGL
 *
 * @note Screen must already be initialized (LCD_1IN69_Init() called).
 */
void lv_port_disp_init(void)
{
    /*--------------------------------------------------
     * Initialize draw buffer
     * Using single partial buffer (not double-buffered)
     *--------------------------------------------------*/
    lv_disp_draw_buf_init(&draw_buf,
                          disp_buf,   /* Buffer 1 */
                          NULL,       /* No buffer 2 (single buffer) */
                          LV_PORT_DISP_BUF_SIZE);

    /*--------------------------------------------------
     * Initialize display driver
     *--------------------------------------------------*/
    lv_disp_drv_init(&disp_drv);

    /* Set display resolution */
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 280;

    /* Set draw buffer */
    disp_drv.draw_buf = &draw_buf;

    /* Set flush callback */
    disp_drv.flush_cb = lv_port_disp_flush;

    /* Disable full refresh (use partial updates) */
    disp_drv.full_refresh = 0;

    /* Set rotation (LV_DISP_ROT_NONE = 0°, portrait) */
    disp_drv.rotated = 0;

    /*--------------------------------------------------
     * Register the driver
     *--------------------------------------------------*/
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    /* Optional: Set display as default */
    if (disp) {
        lv_disp_set_default(disp);
    }

    /* Debug output */
    // usb_printf("[LVGL] Display driver initialized\r\n");
    // usb_printf("       Buffer: %d lines (%d pixels, %lu bytes)\r\n",
    //            LV_PORT_DISP_BUF_HEIGHT,
    //            LV_PORT_DISP_BUF_SIZE,
    //            LV_PORT_DISP_BUF_SIZE * 2);
}

/*-------------------------------------------
 *  DMA completion notification
 *------------------------------------------*/

/**
 * @brief Notify LVGL that DMA transfer is complete
 *
 * This function should be called from HAL_SPI_TxCpltCallback
 * after raising the CS pin.
 */
void lv_port_disp_dma_complete(void)
{
    if (active_disp_drv != NULL) {
        lv_disp_flush_ready(active_disp_drv);
    }
}

/*-------------------------------------------
 *  SPI DMA callback (overrides weak HAL definition)
 *------------------------------------------*/

/**
 * @brief SPI Transmit Complete Callback
 * @param hspi SPI handle
 * @retval None
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    /* Confirm interrupt is from SPI1 (LCD) */
    if (hspi->Instance == SPI1) {
        /* DMA transfer complete, raise CS pin */
        LCD_1IN69_CS_1;

        /* Clear DMA busy flag in LCD driver */
        LCD_1IN69_DMAClearBusy();

        /* Notify LVGL that flush is complete */
        lv_port_disp_dma_complete();

        /* Optional debug output (if USB printf enabled) */
        // usb_printf("[DMA] SPI1 transfer complete\r\n");
    }
}