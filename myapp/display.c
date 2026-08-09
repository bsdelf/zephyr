#include "display.h"

#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

static uint8_t draw_buf[168 * 384 / 8] = {0};

#define NATIVE_WIDTH  168
#define NATIVE_HEIGHT 384

#define ROTATED_WIDTH  384
#define ROTATED_HEIGHT 168

#include <stdint.h>

/**
 * @brief 將 8 行 8 列的 1-bit 矩陣順時針旋轉 90 度
 * @param src 輸入的 8 個位元組（代表 8x8 像素）
 * @param dst 輸出的 8 個位元組
 */
static void rotate_8x8_clockwise(const uint8_t src[8], uint8_t dst[8])
{
	uint32_t low = 0, high = 0;

	low = ((uint32_t)src[0] << 0) | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
	      ((uint32_t)src[3] << 24);
	high = ((uint32_t)src[4] << 0) | ((uint32_t)src[5] << 8) | ((uint32_t)src[6] << 16) |
	       ((uint32_t)src[7] << 24);

	uint32_t t;
	t = (low ^ (low >> 7)) & 0x00AA00AAU;
	low = low ^ t ^ (t << 7);
	t = (high ^ (high >> 7)) & 0x00AA00AAU;
	high = high ^ t ^ (t << 7);

	t = (low ^ (low >> 14)) & 0x0000FFFFU;
	low = low ^ t ^ (t << 14);
	t = (high ^ (high >> 14)) & 0x0000FFFFU;
	high = high ^ t ^ (t << 14);

	t = (low ^ (high << 4)) & 0x0F0F0F0FU;
	low = low ^ t;
	high = high ^ (t >> 4);

	dst[0] = (low >> 0) & 0xFF;
	dst[1] = (low >> 8) & 0xFF;
	dst[2] = (low >> 16) & 0xFF;
	dst[3] = (low >> 24) & 0xFF;
	dst[4] = (high >> 0) & 0xFF;
	dst[5] = (high >> 8) & 0xFF;
	dst[6] = (high >> 16) & 0xFF;
	dst[7] = (high >> 24) & 0xFF;
}

/**
 * @brief 將整張 384x168 的 1-bit 影像軟體旋轉為 168x384
 * @note 384 像素 = 48 位元組 (橫向)，168 像素 = 21 位元組 (橫向)
 */
void rotate_image_1bit_90(const uint8_t *src_buf, uint8_t *dst_buf)
{
	// 來源影像（邏輯）：寬 384 (48 欄位元組)，高 168 行
	// 目標影像（物理）：寬 168 (21 欄位元組)，高 384 行
	uint8_t src_block[8];
	uint8_t dst_block[8];

	for (int y = 0; y < 168; y += 8) {
		for (int x = 0; x < 384; x += 8) {
			// 1. 抽取 8x8 區塊
			for (int i = 0; i < 8; ++i) {
				src_block[i] = src_buf[(y + i) * 48 + (x / 8)];
			}

			// 2. 進行高速旋轉
			rotate_8x8_clockwise(src_block, dst_block);

			// 3. 寫入旋轉後的物理緩衝區 (新座標: nx = 168 - 8 - y, ny = x)
			int target_x = 168 - 8 - y;
			for (int i = 0; i < 8; ++i) {
				int target_y = x + i;
				dst_buf[target_y * 21 + (target_x / 8)] = dst_block[i];
			}
		}
	}
}

static void my_rotate_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	// 因為設定了 FULL_REFRESH，px_map 包含了完整的邏輯 384x168 畫面資訊
	// 執行旋轉轉置：將 px_map (384x168) 轉換至 physical_fb (168x384)
	rotate_image_1bit_90(px_map, draw_buf);

	// 準備發送給 ST730x 驅動程式的物理邊界
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(draw_buf),
		.width = 168,
		.height = 384,
		.pitch = 168,
	};

	// 透過 Zephyr API 直接將旋轉後的資料寫入硬體
	const struct device *display_dev = (const struct device *)lv_display_get_user_data(disp);
	display_write(display_dev, 0, 0, &desc, draw_buf);

	// 必須通知 LVGL 該批次（全畫面）已完成刷屏
	lv_display_flush_ready(disp); //
}

void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	// Retrieve the raw Zephyr device pointer we stored during initialization

	// Get width and height of the current update area block
	int32_t w = lv_area_get_width(area);
	int32_t h = lv_area_get_height(area);

	printk("w=%d, h=%d, y1=%d, y2=%d, x1=%d, x2=%d\n", w, h, area->x1, area->x2, area->y1,
	       area->y2);

	memset(draw_buf, 0, sizeof(draw_buf));
	for (int src_y = 0; src_y < ROTATED_HEIGHT; src_y++) {
		for (int src_x = 0; src_x < ROTATED_WIDTH; src_x++) {

			/* 1. Extract the bit value from the LVGL 1-bit landscape map */
			/* Byte index in source layout: (y * (width / 8)) + (x / 8) */
			int src_byte_idx = (src_y * ROTATED_WIDTH + src_x) / 8;
			/* LVGL usually packs msb-first or lsb-first; adjust bit index if pixels
			 * flip */
			int src_bit_idx = 7 - (src_x % 8);

			bool pixel_is_on = (px_map[src_byte_idx] >> src_bit_idx) & 0x01;

			if (pixel_is_on) {
				/* 2. Map coordinates to a 90-degree clockwise portrait layout */
				/* Landscape (x, y) -> Portrait (X_new, Y_new) */
				int dest_x = (ROTATED_HEIGHT - 1) - src_y;
				int dest_y = src_x;

				/* 3. Pack the bit into the native 168-width portrait buffer */
				int dest_byte_idx = (dest_y * ROTATED_HEIGHT + dest_x) / 8;
				int dest_bit_idx = 7 - (dest_x % 8);

				if (dest_byte_idx >= sizeof(draw_buf)) {
					printk("out of range\n");
					continue;
				}

				draw_buf[dest_byte_idx] |= (1 << dest_bit_idx);
			}
		}
	}

	// lv_area_t rotated_area = *area;
	// lv_display_rotate_area(disp, &rotated_area);

	// Pack your transformed structure and fire it to the display driver
	struct display_buffer_descriptor desc = {.buf_size = (NATIVE_WIDTH * NATIVE_HEIGHT) / 8,
						 .width = NATIVE_WIDTH,
						 .height = NATIVE_HEIGHT,
						 .pitch = NATIVE_WIDTH};

	// Forward the drawing vector directly to Zephyr's peripheral layer
	const struct device *display_dev = (const struct device *)lv_display_get_user_data(disp);
	display_write(display_dev, 0, 0, &desc, draw_buf);

	printk("my flush cb x: %d, y: %d\n", 0, 0);

	// IMPORTANT: Tell the LVGL graphics core that the display transmission completed
	lv_display_flush_ready(disp);
}

void init_rotated_display(struct device *display_dev)
{
	// Create an LVGL display object
	// lv_display_t *disp = lv_display_create(384, 168); // Use your desired target layout
	lv_display_t *disp = lv_disp_get_default();

	// Tell LVGL that the physical panel is rotated relative to your layout
	// lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
	// 		       LV_DISPLAY_RENDER_MODE_PARTIAL);

	lv_display_set_user_data(disp, display_dev);

	// Associate your low-level Zephyr flush callback
	lv_display_set_flush_cb(disp, my_flush_cb);
	lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90); // Or LV_DISPLAY_ROTATION_270
}
