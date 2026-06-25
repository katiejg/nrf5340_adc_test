/* SPDX-License-Identifier: Apache-2.0
 *
 * Wiring layer only.  Does not reference any ADC or BLE implementation
 * details directly — those are encapsulated in their respective modules.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include "adc_sampler.h"
#include "ble_sender.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* PIN DEFINITIONS */
#define BTN_NODE DT_ALIAS(sw0) // Button 1
#define LED_PIN DT_ALIAS(led0) // LED1

/* STRUCTS */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_PIN, gpios);
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(BTN_NODE, gpios);
static struct gpio_callback button_cb_data;

/* SETUP LED */
void led_setup() {
	int ret;
	// Check if ready
	if (!gpio_is_ready_dt(&led)) {
		return;
	}

	// Configure LED
      ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return;
	}

      gpio_pin_set_dt(&led, 0); // Set LED pin LOW
	return;
}

/* BUTTON INTERRUPT */
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	if (gpio_is_ready_dt(&led) && gpio_pin_get_dt(&led)) {
		adc_sampler_stop();
	} else {
		adc_sampler_start(); // TODO: doesn't actually start up properly on the BLE side
	}
	gpio_pin_toggle_dt(&led);
	k_msleep(10); // optional: small debounce? 
}

/* SETUP BUTTON AND INTERRUPT */
void btn_setup() {
	int ret;
	if (!gpio_is_ready_dt(&btn)) {
		return;
	}

	// Configure button
      ret = gpio_pin_configure_dt(&btn, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) {
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(btn.pin));
	gpio_add_callback(btn.port, &button_cb_data);

}

int main(void)
{
	int err;
	led_setup();

	err = adc_sampler_init();
	if (err) {
		LOG_ERR("adc_sampler_init: %d", err);
		return err;
	}

	err = ble_sender_init();
	if (err) {
		LOG_ERR("ble_sender_init: %d", err);
		return err;
	}

	// Automatic start
	adc_sampler_start();
	gpio_pin_set_dt(&led, 1); // solid on = recording
	btn_setup(); // Set up button afterwards

	return 0;
}
