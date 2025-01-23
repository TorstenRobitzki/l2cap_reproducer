/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/l2cap.h>
#include <bluetooth/services/cgms.h>
#include <sfloat.h>

#include <zephyr/logging/log.h>

#include <cstdint>
#include <array>

LOG_MODULE_REGISTER(l2cap_reproducer, LOG_LEVEL_DBG);

#define APP_GLUCOSE_MIN    88
#define APP_GLUCOSE_MAX    92
#define APP_GLUCOSE_STEP   0.1f

static bool session_state;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
				BT_UUID_16_ENCODE(BT_UUID_CGMS_VAL),
				BT_UUID_16_ENCODE(BT_UUID_DIS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (err) {
		printk("Failed to connect to %s, err 0x%02x %s\n", addr, err,
		       bt_hci_err_to_str(err));
	} else {
		printk("Connected %s\n", addr);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Disconnected from %s, reason 0x%02x %s\n", addr, reason, bt_hci_err_to_str(reason));
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d %s\n", addr, level, err,
		       bt_security_err_to_str(err));
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	printk("Pairing key is %06d.\n", passkey);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};

static void cgms_session_state_changed(const bool state)
{
	session_state = state;
	if (state) {
		printk("Session starts.\n");
	} else {
		printk("Session stops.\n");
	}
}

static bt_l2cap_chan* l2cap_channel_ = nullptr;

static const std::uint8_t random_data[ 1024 ] = { 1, 2, 3, 4, 5, 6, 7 };

static bool is_connected = false;

NET_BUF_POOL_FIXED_DEFINE(
    mqtt_sn_messages_over_l2cap,
    5,
    std::size( random_data ) + BT_L2CAP_SDU_CHAN_SEND_RESERVE,
    CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static void try_send_random_stuff()
{
	if ( is_connected && l2cap_channel_ )
	{
        net_buf * const buf = net_buf_alloc_fixed(
            &mqtt_sn_messages_over_l2cap, K_FOREVER);

        if ( !buf )
        {
            return;
        }

        const std::size_t max_message_size = BT_L2CAP_LE_CHAN(l2cap_channel_)->tx.mtu;
        const std::size_t transmit_size = std::min< std::size_t >( max_message_size, std::size( random_data ) );

        net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
        net_buf_add_mem(buf, random_data, transmit_size);

        const int rc = bt_l2cap_chan_send(l2cap_channel_, buf);

        if (rc < 0) {
            net_buf_unref(buf);
            return;
        }

        assert( rc == 0 || rc == -ENOTCONN );
	}
}

static void init_l2cap()
{
    static bt_conn_cb bt_conn_callbacks = {
        .connected = [](struct bt_conn *conn, uint8_t err) -> void {
        	is_connected = true;
            LOG_INF("connected: %d", (int)err);
        },
        .disconnected = [](struct bt_conn *conn, uint8_t reason) -> void {
        	is_connected = false;
            LOG_INF("disconnected: %d", (int)reason);
        }
    };

    [[maybe_unused]] const int bt_cb_rc = bt_conn_cb_register(&bt_conn_callbacks);
    assert( bt_cb_rc == 0 );

    static bt_gatt_cb gatt_callbacks = {
        .att_mtu_updated = [](struct bt_conn *, uint16_t tx, uint16_t rx)
        {
            LOG_INF("Updated MTU: TX: %d RX: %d bytes", tx, rx);
        }
    };

    bt_gatt_cb_register(&gatt_callbacks);

    static bt_l2cap_server l2cap_channel = {
        .psm = 0,
        // TODO
        .sec_level = BT_SECURITY_L1, //BT_SECURITY_L4,
        .accept = [](struct bt_conn* conn, struct bt_l2cap_server* /*server*/, struct bt_l2cap_chan **chan) -> int
        {
            static const bt_l2cap_chan_ops channel_callbacks = {
                .connected = [](struct bt_l2cap_chan */*chan*/)
                {
                    LOG_INF("incomming L2CAP connection...");
					try_send_random_stuff();
					try_send_random_stuff();
					try_send_random_stuff();
					try_send_random_stuff();
					try_send_random_stuff();
					try_send_random_stuff();
                },
                .disconnected = nullptr,
                .encrypt_change = nullptr,
                .alloc_seg = nullptr,
                .alloc_buf = nullptr,
                .recv = [](struct bt_l2cap_chan */*chan*/, struct net_buf *buf) -> int {
                    return buf->len;
                },
                .sent = [](struct bt_l2cap_chan */*chan*/){
					try_send_random_stuff();
                },
                .status = nullptr,
                .released = nullptr,
                .reconfigured = nullptr,
                .seg_recv = nullptr
            };

            /*
             * !!! The caller is not expecting to have a bt_l2cap_chan
             * beeing returned, but a bt_l2cap_le_chan that contains a
             * bt_l2cap_chan (poor mans inheritance)
             */
            static bt_l2cap_le_chan channel = {
                .chan = {
                    .conn = conn,
                    .ops  = &channel_callbacks
                }
             };

            l2cap_channel_ = &channel.chan;
            *chan = &channel.chan;
            return 0;
        }
    };

    [[maybe_unused]] const int l2cap_channel_rc = bt_l2cap_server_register( &l2cap_channel );
    LOG_ERR( "l2cap_channel_rc: %d", l2cap_channel_rc);
    assert( l2cap_channel_rc == 0 );
    LOG_INF( "psm: %d", (int)l2cap_channel.psm );
}

int main(void)
{
	int err;
	const uint8_t measurement_interval = 1; /* time in minutes. */
	/* The time it will try to submit a record if last attempt fails. */
	const uint8_t retry_count = 3;
	/* The time interval between two attempts. Unit is second. */
	const uint8_t retry_interval = 1;
	float glucose = APP_GLUCOSE_MIN;
	struct bt_cgms_measurement result;
	struct bt_cgms_cb cb;
	struct bt_cgms_init_param params;

	printk("Starting Bluetooth Peripheral CGM example\n");

	bt_conn_auth_cb_register(&auth_cb_display);

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	init_l2cap();

	params.type = BT_CGMS_FEAT_TYPE_CAP_PLASMA;
	params.sample_location = BT_CGMS_FEAT_LOC_FINGER;
	/* The session will run 1 hour */
	params.session_run_time = 1;
	/* The initial communication interval will be 5 minutes */
	params.initial_comm_interval = 5;

	cb.session_state_changed = cgms_session_state_changed;
	params.cb = &cb;

	err = bt_cgms_init(&params);
	if (err) {
		printk("Error occurred when initializing cgm service (err %d)\n", err);
		return 0;
	}
	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printk("Advertising successfully started\n");
	/* Submit the measured glucose result in main loop. */
	while (1) {
		if (session_state) {
			/* Users can implement the routine that gets the glucose measurement.
			 * Here, we use dummy glucose results, which increase from APP_GLUCOSE_MIN
			 * to APP_GLUCOSE_MAX, with step of APP_GLUCOSE_STEP.
			 */
			glucose += APP_GLUCOSE_STEP;
			if (glucose > APP_GLUCOSE_MAX) {
				glucose = APP_GLUCOSE_MIN;
			}
			result.glucose = sfloat_from_float(glucose);

			/* Check return value. If it is error code, which means submission
			 * to database fails, retry until it reaches limit.
			 */
			for (int i = 0; i < retry_count; i++) {
				err = bt_cgms_measurement_add(result);
				if (err == 0) {
					break;
				}
				k_sleep(K_SECONDS(retry_interval));
			}
			if (err < 0) {
				printk("Cannot submit measurement result. Record discarded.\n");
			}
		}
		/* Sleep until next sampling time arrives. */
		k_sleep(K_MINUTES(measurement_interval));
	}
}
