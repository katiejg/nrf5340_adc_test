/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ADC_SAMPLER_H_
#define ADC_SAMPLER_H_

#include <zephyr/kernel.h>
#include <stdint.h>

/* Must match the number of channels declared in boards/*.overlay */
#define ADC_NUM_CHANNELS 3

/* ── Contract ─────────────────────────────────────────────────────────────── */

struct adc_sample {
	uint32_t timestamp_us;
	int16_t  raw[ADC_NUM_CHANNELS];
	int32_t  mv[ADC_NUM_CHANNELS];
};

/* Shared queue: ADC is the producer, BLE sender is the consumer.
 * Depth 20 = 200 ms of headroom at 100 Hz.
 * Defined in adc_sampler.c; BLE side includes this header for the extern. */
extern struct k_msgq adc_sample_q;

/* ── API ──────────────────────────────────────────────────────────────────── */

int  adc_sampler_init(void);
void adc_sampler_start(void);
void adc_sampler_stop(void);

#endif /* ADC_SAMPLER_H_ */
