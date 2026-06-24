/* SPDX-License-Identifier: Apache-2.0 */
#include "adc_sampler.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(adc_sampler, LOG_LEVEL_DBG);

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)
};

BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_NUM_CHANNELS,
	     "overlay io-channels count must equal ADC_NUM_CHANNELS");

/* ── Queue (contract with BLE consumer) ──────────────────────────────────── */

K_MSGQ_DEFINE(adc_sample_q, sizeof(struct adc_sample), 20, 4);

/* ── Internal state ───────────────────────────────────────────────────────── */

#define SAMPLE_INTERVAL_MS 10   /* 100 Hz */

static struct k_timer sample_timer;
static K_SEM_DEFINE(sample_sem, 0, 1);
static volatile bool running;

static void timer_fn(struct k_timer *t)
{
	ARG_UNUSED(t);
	k_sem_give(&sample_sem);
}

/* ── Verified blocking read (preserved from original implementation) ───────
 *
 * Each adc_read() takes ~10–50 µs; three channels well under 1 ms total.
 * The k_timer + semaphore above fires every 10 ms, so the 100 Hz target is
 * met as long as sample_all_channels() finishes before the next tick.
 *
 * TODO(DMA — phase 2): Replace this function body with adc_read_async() +
 * k_poll() so the thread yields during conversion and the CPU can enter
 * sleep between samples.
 *
 * TODO(DMA — phase 3, full CPU sleep): Configure SAADC scan mode triggered
 * by TIMER via DPPI; handle the SAADC END event in a callback that writes
 * directly into adc_sample_q.  The CPU can then stay in System ON Low Power
 * between callbacks entirely.
 */

static void sample_all_channels(struct adc_sample *out)
{
	int16_t buf;
	struct adc_sequence sequence = {
		.buffer      = &buf,
		.buffer_size = sizeof(buf),
	};

	out->timestamp_us = k_cyc_to_us_floor32(k_cycle_get_32());

	for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		int err = adc_sequence_init_dt(&adc_channels[i], &sequence);
		if (err < 0) {
			LOG_ERR("sequence init ch%u: %d", i, err);
			out->raw[i] = 0;
			out->mv[i]  = 0;
			continue;
		}

		err = adc_read(adc_channels[i].dev, &sequence);
		if (err < 0) {
			LOG_ERR("adc_read ch%u: %d", i, err);
			out->raw[i] = 0;
			out->mv[i]  = 0;
			continue;
		}

		int32_t val_mv = (int32_t)buf;
		out->raw[i] = buf;

		LOG_INF("ADC reading[%u us]: %s, channel %d: Raw: %d",
			out->timestamp_us,
			adc_channels[i].dev->name,
			adc_channels[i].channel_id,
			out->raw[i]);

		err = adc_raw_to_millivolts_dt(&adc_channels[i], &val_mv);
		if (err < 0) {
			LOG_WRN("ch%u: mV not available", i);
			out->mv[i] = 0;
		} else {
			LOG_INF("ch%u: %d mV", i, val_mv);
			out->mv[i] = val_mv;
		}
	}
}

/* ── Sampler thread ───────────────────────────────────────────────────────── */

static void sampler_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		k_sem_take(&sample_sem, K_FOREVER);

		if (!running) {
			continue;
		}

		struct adc_sample s;
		sample_all_channels(&s);

		if (k_msgq_put(&adc_sample_q, &s, K_NO_WAIT) != 0) {
			LOG_WRN("queue full, sample dropped");
		}
	}
}

K_THREAD_DEFINE(sampler_tid, 1024, sampler_thread_fn,
		NULL, NULL, NULL, 5, 0, 0);

/* ── Public API ───────────────────────────────────────────────────────────── */

int adc_sampler_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		if (!adc_is_ready_dt(&adc_channels[i])) {
			LOG_ERR("ADC device %s not ready",
				adc_channels[i].dev->name);
			return -ENODEV;
		}

		int err = adc_channel_setup_dt(&adc_channels[i]);
		if (err < 0) {
			LOG_ERR("channel setup ch%u: %d", i, err);
			return err;
		}
	}

	k_timer_init(&sample_timer, timer_fn, NULL);
	LOG_INF("ADC sampler ready, %u channels", ADC_NUM_CHANNELS);
	return 0;
}

void adc_sampler_start(void)
{
	running = true;
	k_timer_start(&sample_timer, K_NO_WAIT, K_MSEC(SAMPLE_INTERVAL_MS));
	LOG_INF("ADC sampling started @ %d Hz", 1000 / SAMPLE_INTERVAL_MS);
}

void adc_sampler_stop(void)
{
	running = false;
	k_timer_stop(&sample_timer);
	LOG_INF("ADC sampling stopped");
}
