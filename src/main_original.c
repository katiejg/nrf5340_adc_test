// /* SPDX-License-Identifier: Apache-2.0
//  *
//  * Wiring layer only.  Does not reference any ADC or BLE implementation
//  * details directly — those are encapsulated in their respective modules.
//  */

// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>

// #include "adc_sampler.h"
// #include "ble_sender.h"

// LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// int main(void)
// {
// 	int err;

// 	err = adc_sampler_init();
// 	if (err) {
// 		LOG_ERR("adc_sampler_init: %d", err);
// 		return err;
// 	}

// 	err = ble_sender_init();
// 	if (err) {
// 		LOG_ERR("ble_sender_init: %d", err);
// 		return err;
// 	}

// 	adc_sampler_start();

// 	return 0;
// }
