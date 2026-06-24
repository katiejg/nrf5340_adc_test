/* SPDX-License-Identifier: Apache-2.0
 *
 * BLE sender module.
 *
 * Architecture contract:
 *   - Knows only about adc_sample_q and struct adc_sample (from adc_sampler.h)
 *   - Has zero knowledge of how ADC sampling is implemented
 *   - Single direction: NUS TX only; RX callback is NULL
 *
 * Packet format sent to PC (little-endian, packed):
 *   [seq:u16][count:u8][t0_us:u32][raw[0..BATCH-1][0..CH-1]:i16]
 *   = 7 + BATCH_SIZE * ADC_NUM_CHANNELS * 2 bytes
 *   = 7 + 5*3*2 = 37 bytes  →  requires ATT MTU >= 40
 *     (set CONFIG_BT_L2CAP_TX_MTU=65 in prj.conf)
 */

#include "ble_sender.h"
#include "adc_sampler.h"   /* contract: adc_sample_q, struct adc_sample */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <bluetooth/services/nus.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_sender, LOG_LEVEL_DBG);

#define CON_STATUS_LED  DK_LED2

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* ── Batch parameters ─────────────────────────────────────────────────────── */

/* Samples per BLE notify.  At 100 Hz, BATCH_SIZE=5 → 20 notifies/s (50 ms
 * per notify), which keeps the connection event count very low. */
#define BATCH_SIZE 5

struct __packed ble_adc_packet {
	uint16_t seq;                              /* rolling, for gap detection */
	uint8_t  count;                            /* always BATCH_SIZE for now */
	uint32_t t0_us;                            /* timestamp of first sample */
	int16_t  raw[BATCH_SIZE][ADC_NUM_CHANNELS];
};

/* ── BLE state ────────────────────────────────────────────────────────────── */

static K_SEM_DEFINE(ble_init_ok, 0, 1);
static struct bt_conn *current_conn;
static struct k_work   adv_work;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

/* ── Connection callbacks ─────────────────────────────────────────────────── */

static void adv_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2,
				  ad, ARRAY_SIZE(ad),
				  sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("adv start failed: %d", err);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (err) {
		LOG_ERR("connect failed %s err 0x%02x", addr, err);
		return;
	}
	LOG_INF("connected %s", addr);
	current_conn = bt_conn_ref(conn);
	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("disconnected %s reason 0x%02x", addr, reason);

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
		dk_set_led_off(CON_STATUS_LED);
	}
}

static void recycled_cb(void)
{
	k_work_submit(&adv_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected,
	.disconnected = disconnected,
	.recycled     = recycled_cb,
};

/* RX path intentionally absent: single-direction design. */
static struct bt_nus_cb nus_cb = {
	.received = NULL,
};

/* ── BLE write thread ─────────────────────────────────────────────────────── */

static void ble_write_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_sem_take(&ble_init_ok, K_FOREVER);

	struct ble_adc_packet pkt;
	uint16_t seq = 0;

	while (1) {
		/* Accumulate BATCH_SIZE samples from the queue (blocks until
		 * all slots are filled). */
		for (uint8_t i = 0; i < BATCH_SIZE; i++) {
			struct adc_sample s;

			k_msgq_get(&adc_sample_q, &s, K_FOREVER);

			if (i == 0) {
				pkt.t0_us = s.timestamp_us;
			}
			memcpy(pkt.raw[i], s.raw,
			       sizeof(int16_t) * ADC_NUM_CHANNELS);
		}

		pkt.seq   = seq++;
		pkt.count = BATCH_SIZE;

		if (current_conn) {
			int err = bt_nus_send(NULL,
					      (const uint8_t *)&pkt,
					      sizeof(pkt));
			if (err) {
				LOG_WRN("bt_nus_send: %d", err);
			}
		}
	}
}

K_THREAD_DEFINE(ble_write_tid, 2048, ble_write_thread_fn,
		NULL, NULL, NULL, 7, 0, 0);

/* ── Public API ───────────────────────────────────────────────────────────── */

int ble_sender_init(void)
{
	int err;

	err = dk_leds_init();
	if (err) {
		LOG_ERR("dk_leds_init: %d", err);
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable: %d", err);
		return err;
	}
	LOG_INF("BT enabled");

	err = bt_nus_init(&nus_cb);
	if (err) {
		LOG_ERR("bt_nus_init: %d", err);
		return err;
	}

	k_work_init(&adv_work, adv_work_handler);
	k_sem_give(&ble_init_ok);
	k_work_submit(&adv_work);

	return 0;
}
