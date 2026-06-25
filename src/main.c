// /**
//  * @file main.c
//  * @author Katie Jiang
//  * @brief Multi-channel ADC reads on the nRF5340, based on ESP32 version
//  * @version 0.1
//  * @date 2026-06-22
//  */

// #include <stdio.h>

// #include <zephyr/kernel.h>
// #include <zephyr/device.h>
// #include <zephyr/devicetree.h>
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/drivers/adc.h>
// #include <zephyr/logging/log.h>
// #include <zephyr/sys/util.h>

// /* START LOG */
// LOG_MODULE_REGISTER(nRF_ADC, LOG_LEVEL_DBG);

// /* PIN DEFINITIONS */
// // #define CHIPSELECT
// #define BTN_PIN DT_ALIAS(sw0) // Button 1
// #define LED_PIN DT_ALIAS(led0) // LED1

// /* ADC SPEC CHANNEL SELECTION */
// // ADC channels are specified and configured in boards/*.overlay
// #define DT_SPEC_AND_COMMA(node_id, prop, idx) \
// 	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

// /* CONST STRUCTS */
// static const struct adc_dt_spec adc_channels[] = { // adc channels
//       DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)
// };
// static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_PIN, gpios);
// static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(BTN_PIN, gpios);

// /* CONSTANTS & VARIABLES */
// const long interval = 10000;
// bool is_running = false;
// int32_t raw_vals[ARRAY_SIZE(adc_channels)];
// int32_t voltages[ARRAY_SIZE(adc_channels)];
// unsigned long initialTime;
// unsigned long currentTime;
// unsigned long expectedTime = 0;

// void blinkLED(void) {
//       gpio_pin_toggle_dt(&led);
//       k_msleep(200);
// }

// // TODO: Update to an interrupt
// void waitForButtonPress() {
//       // wait for button to be released first (debounce)
//       while (gpio_pin_get_dt(&btn));
//       k_msleep(50);
//       // now wait for next press
//       while (!gpio_pin_get_dt(&btn));
//       k_msleep(50);
// }

// void setup() {
//       int err;
//       // Configure in/out pins
//       gpio_pin_configure_dt(&btn, GPIO_INPUT | GPIO_PULL_UP);
//       gpio_pin_configure_dt(&led, GPIO_OUTPUT);
//       gpio_pin_set_dt(&led, 0); // Set LED pin LOW

//       // Configure ADC channels
//       for (size_t i=0U; i<ARRAY_SIZE(adc_channels); i++) {
//             if (!adc_is_ready_dt(&adc_channels[i])) {
//                   LOG_ERR("ADC controller device %s not ready", adc_channels[i].dev->name);
//                   blinkLED();
//                   return;
//             }

//             err = adc_channel_setup_dt(&adc_channels[i]);
//             if (err < 0) {
//                   LOG_ERR("Could not setup channel #%d (%d)", 0, err);
//                   blinkLED();
//                   return;
//             }
//       }

//       // initSD omitted
//       is_running = true;
//       gpio_pin_set_dt(&led, 1); // solid on = recording
//       // printk("Setup complete");
// }

// int main(void) {
//       int err;
//       setup();
//       // Define ADC sequence & buffer
//       int16_t buf;
//       struct adc_sequence sequence = {
//             .buffer = &buf,
//             .buffer_size = sizeof(buf),
//       };

//       // void loop()
//       while (1) {
//             if (!is_running) {
//                   // printk("Press button to start new recording...");
//                   gpio_pin_set_dt(&led, 0);
//                   waitForButtonPress();
      
//                   expectedTime = 0;
//                   is_running = true;
//                   gpio_pin_set_dt(&led, 1);
//                   continue;
//             }

//             initialTime = k_cyc_to_us_floor32(k_cycle_get_32()); // equivalent to micros()
//             while (is_running) {
//                   currentTime = k_cyc_to_us_floor32(k_cycle_get_32());
//                   if (currentTime - initialTime >= expectedTime) {
//                         expectedTime += interval;
      
//                         if (gpio_pin_get_dt(&btn)) {
//                               while (!gpio_pin_get_dt(&btn));
//                               is_running = false;
//                               gpio_pin_set_dt(&led, 0); // off = stopped
//                               // printk("Recording stopped");
//                               break;
//                         }
      
//                         for (size_t i=0U; i<ARRAY_SIZE(adc_channels); i++) {
//                               // Initialize ADC sequence
//                               err = adc_sequence_init_dt(&adc_channels[i], &sequence);
//                               if (err < 0) {
//                                     LOG_ERR("Could not initialize sequence\n");
//                                     return 0;
//                               }
      
//                               int32_t val_mv;
      
//                               // Analog read
//                               err = adc_read(adc_channels[i].dev, &sequence);
//                               if (err < 0) {
//                                     LOG_ERR("Could not read (%d)\n", err);
//                                     continue;
//                               }
      
//                               val_mv = (int)buf;
//                               raw_vals[i] = val_mv;
      
//                               unsigned long time_diff = k_cyc_to_us_floor32(k_cycle_get_32()) - initialTime;
//                               LOG_INF("ADC reading[%lu]: %s, channel %d: Raw: %d", time_diff, adc_channels[i].dev->name, 
//                                     adc_channels[i].channel_id, raw_vals[i]);
                              
//                               // Convert raw value to mV
//                               err = adc_raw_to_millivolts_dt(&adc_channels[i], &val_mv);
//                               if (err < 0) {
//                               LOG_WRN(" (value in mV not available)\n");
//                               } else {
//                                     LOG_INF(" = %d mV", val_mv);
//                               }
      
//                               voltages[i] = val_mv;
//                               // This sleep is necessary in order to print periodically
//                               // and makes the readings more accurate for some reason...
//                               // TODO: Adjust sleep to match sampling rate
//                               k_sleep(K_MSEC(1000));
//                         }
//                   }
//             }
//       }
// }