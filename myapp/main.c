#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
// #include <lvgl.h> // 如果启用了 LVGL

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/sys/reboot.h>
#include <pico/bootrom.h>

// #include "display.h"
//
#define BUF_SIZE 8064
uint8_t buf[BUF_SIZE] = {0};

void clear_st7305_screen(struct device *display_dev, uint32_t counter)
{
	uint8_t ch = 0x1 << (counter % 8);
	memset(buf, ch, BUF_SIZE);
	struct display_buffer_descriptor desc = {
		.buf_size = BUF_SIZE,
		.width = 168,
		.height = 384,
		.pitch = 168, // 每行的像素点数
	};

	printk("display write: \n");
	int ret = display_write(display_dev, 0, 0, &desc, buf);
	if (ret != 0) {
		printk("Display write failed: %d\n", ret);
	}
}

void draw(struct device *display_dev, uint32_t counter)
{
	memset(buf, 0, BUF_SIZE);

	uint32_t offset = 0;
	memset(buf, 0xff, 168 / 8 * (384 / 2));
	// memset(buf + offset + counter % (168 / 8), 0xff, 168 / 8);
	// memset(buf + 168 / 8 + counter % (168 / 8), 0x7f, 1);
	// memset(buf + 20, 0xf, 1);
	// memset(buf + 21, 0xf, 1);
	// memset(buf + 200 * 1, 0x2, 200);
	// memset(buf + 200 * 2, 0x4, 200);
	// memset(buf + 200 * 3, 0x8, 200);

	struct display_buffer_descriptor desc = {
		.buf_size = BUF_SIZE,
		.width = 168,
		.height = 384,
		.pitch = 168, // 每行的像素点数
	};
	uint32_t start = k_cycle_get_32();
	int ret = display_write(display_dev, 0, 0, &desc, buf);
	uint32_t end = k_cycle_get_32();
	uint32_t elapsed = k_cyc_to_ms_floor32(end - start);
	printk("display write elapsed: %u\n", elapsed);
	if (ret != 0) {
		printk("Display write failed: %d\n", ret);
	}
}

static const struct gpio_dt_spec flashbutton = GPIO_DT_SPEC_GET(DT_ALIAS(flashbutton), gpios);
static struct gpio_callback button_cb_data;

void bootloader_trigger_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	printk("GPIO Triggered! Rebooting into UF2 Bootloader...\n");

	/* Wait briefly for serial flush or debouncing if necessary */
	k_msleep(50);

	reset_usb_boot(0, 0);
}

int main(void)
{
	gpio_pin_configure_dt(&flashbutton, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&flashbutton, GPIO_INT_EDGE_TO_ACTIVE);

	gpio_init_callback(&button_cb_data, bootloader_trigger_handler, BIT(flashbutton.pin));
	gpio_add_callback(flashbutton.port, &button_cb_data);

	// 获取设备树中设定的默认显示设备
	struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display_dev)) {
		printk("Error: Display device not ready\n");
		return -0;
	}

	printk("ST7305 Display initialized successfully via Native Zephyr!\n");

	// clear_st7305_screen(display_dev, 0);

	draw(display_dev, 0);

	struct display_capabilities caps;
	display_get_capabilities(display_dev, &caps);
	printk("w: %d, h: %d\n", caps.x_resolution, caps.y_resolution);

	// init_rotated_display(display_dev);

	// lv_obj_t *active_screen = lv_screen_active();

	// 2. Create a basic label container object on top of the active screen
	// lv_obj_t *label = lv_label_create(active_screen);

	// 3. Assign your message text string to the label object

#if CONFIG_LVGL
	// 如果启用了 LVGL，只需正常像写常规 GUI 一样操作控件即可
	printk("try lvgl\n");
#endif

	uint32_t counter = 0;
	while (1) {
#if CONFIG_LVGL
		// lv_label_set_text_fmt(label, "Hello %d", counter);
		// lv_obj_center(label);
		// lv_obj_set_pos(label, 0, counter % 168);
		// lv_timer_handler();
// display_clear_and_show_text("hello world");
#endif
		printk("tick: %d\n", counter++);
		// clear_st7305_screen(display_dev, counter);
		draw(display_dev, counter);
		k_sleep(K_MSEC(500));
	}
	return 0;
}
