/* btp_aics.c - Bluetooth AICS Tester */

/*
 * Copyright (c) 2023 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <errno.h>

#include <zephyr/types.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/micp.h>
#include <zephyr/bluetooth/audio/aics.h>

#include "bap_endpoint.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#define LOG_MODULE_NAME bttester_aics
LOG_MODULE_REGISTER(LOG_MODULE_NAME, CONFIG_BTTESTER_LOG_LEVEL
);

#include "btp/btp.h"

#define BT_AICS_MAX_INPUT_DESCRIPTION_SIZE 16
#define BT_AICS_MAX_OUTPUT_DESCRIPTION_SIZE 16

struct btp_aics_instance aics_client_instance;
struct btp_aics_instance aics_server_instance;

static struct net_buf_simple *rx_ev_buf = NET_BUF_SIMPLE(BT_AICS_MAX_INPUT_DESCRIPTION_SIZE +
							 sizeof(struct btp_aics_description_ev));

static uint8_t aics_supported_commands(const void *cmd, uint16_t cmd_len,
				       void *rsp, uint16_t *rsp_len)
{
	struct btp_aics_read_supported_commands_rp *rp = rsp;

	/* octet 0 */
	tester_set_bit(rp->data, BTP_AICS_READ_SUPPORTED_COMMANDS);
	tester_set_bit(rp->data, BTP_AICS_SET_GAIN);
	tester_set_bit(rp->data, BTP_AICS_MUTE);
	tester_set_bit(rp->data, BTP_AICS_UNMUTE);
	tester_set_bit(rp->data, BTP_AICS_MUTE_DISABLE);
	tester_set_bit(rp->data, BTP_AICS_MAN_GAIN);
	tester_set_bit(rp->data, BTP_AICS_AUTO_GAIN);
	tester_set_bit(rp->data, BTP_AICS_MAN_GAIN_ONLY);
	tester_set_bit(rp->data, BTP_AICS_AUTO_GAIN_ONLY);
	tester_set_bit(rp->data, BTP_AICS_GAIN_SETTING_PROP);
	tester_set_bit(rp->data, BTP_AICS_TYPE);
	tester_set_bit(rp->data, BTP_AICS_STATUS);
	tester_set_bit(rp->data, BTP_AICS_STATE);

	/* octet 1 */
	tester_set_bit(rp->data, BTP_AICS_DESCRIPTION);

	*rsp_len = sizeof(*rp) + 2;

	return BTP_STATUS_SUCCESS;
}

void btp_send_aics_state_ev(struct bt_conn *conn, int8_t gain, uint8_t mute, uint8_t mode)
{
	struct btp_aics_state_ev ev;
	struct bt_conn_info info;

	(void)bt_conn_get_info(conn, &info);
	bt_addr_le_copy(&ev.address, info.le.dst);

	ev.gain = gain;
	ev.mute = mute;
	ev.mode = mode;

	tester_event(BTP_SERVICE_ID_AICS, BTP_AICS_STATE_EV, &ev, sizeof(ev));
}

void btp_send_gain_setting_properties_ev(struct bt_conn *conn, uint8_t units, int8_t minimum,
					 int8_t maximum)
{
	struct btp_gain_setting_properties_ev ev;
	struct bt_conn_info info;

	(void)bt_conn_get_info(conn, &info);
	bt_addr_le_copy(&ev.address, info.le.dst);

	ev.units = units;
	ev.minimum = minimum;
	ev.maximum = maximum;

	tester_event(BTP_SERVICE_ID_AICS, BTP_GAIN_SETTING_PROPERTIES_EV, &ev, sizeof(ev));
}

void btp_send_aics_input_type_event(struct bt_conn *conn, uint8_t input_type)
{
	struct btp_aics_input_type_ev ev;
	struct bt_conn_info info;

	(void)bt_conn_get_info(conn, &info);
	bt_addr_le_copy(&ev.address, info.le.dst);

	ev.input_type = input_type;

	tester_event(BTP_SERVICE_ID_AICS, BTP_AICS_INPUT_TYPE_EV, &ev, sizeof(ev));
}

void btp_aics_status_ev(struct bt_conn *conn, bool active)
{
	struct btp_aics_status_ev ev;
	struct bt_conn_info info;

	(void)bt_conn_get_info(conn, &info);
	bt_addr_le_copy(&ev.address, info.le.dst);

	ev.active = active;

	tester_event(BTP_SERVICE_ID_AICS, BTP_AICS_STATUS_EV, &ev, sizeof(ev));
}

void btp_aics_description_ev(struct bt_conn *conn, uint8_t data_len, char *description)
{
	struct btp_aics_description_ev *ev;
	struct bt_conn_info info;

	(void)bt_conn_get_info(conn, &info);

	net_buf_simple_init(rx_ev_buf, 0);

	ev = net_buf_simple_add(rx_ev_buf, sizeof(*ev));

	bt_addr_le_copy(&ev->address, info.le.dst);

	ev->data_len = data_len;
	memcpy(ev->data, description, data_len);

	tester_event(BTP_SERVICE_ID_AICS, BTP_AICS_DESCRIPTION_EV, ev, sizeof(*ev) + data_len);
}

static uint8_t aics_set_gain(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_set_gain_cmd *cp = cmd;

	LOG_DBG("AICS set gain %d", cp->gain);

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_gain_set(aics_client_instance.aics[i], cp->gain) != 0) {
				return BTP_STATUS_FAILED;
			}
		}

	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++){
			if (bt_aics_gain_set(aics_server_instance.aics[i], cp->gain) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_unmute(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_unmute_cmd *cp = cmd;

	LOG_DBG("AICS Unmute");

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_unmute(aics_client_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}

	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
			if (bt_aics_unmute(aics_server_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_mute(const void *cmd, uint16_t cmd_len,
			 void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_mute_cmd *cp = cmd;

	LOG_DBG("AICS Mute");

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_mute(aics_client_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}

	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
			if (bt_aics_mute(aics_server_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_state(const void *cmd, uint16_t cmd_len,
			  void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_state_cmd *cp = cmd;

	LOG_DBG("AICS State");

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_state_get(aics_client_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
			if (bt_aics_state_get(aics_server_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_type(const void *cmd, uint16_t cmd_len,
			 void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_type_cmd *cp = cmd;

	LOG_DBG("AICS Type");

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_type_get(aics_client_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
			if (bt_aics_type_get(aics_server_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_status(const void *cmd, uint16_t cmd_len,
			   void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_status_cmd *cp = cmd;

	LOG_DBG("AICS Status");

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_status_get(aics_client_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
			if (bt_aics_status_get(aics_server_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_gain_setting_prop(const void *cmd, uint16_t cmd_len,
				      void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_gain_setting_prop_cmd *cp = cmd;

	LOG_DBG("AICS Gain settings properties");

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_gain_setting_get(aics_client_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
			if (bt_aics_gain_setting_get(aics_server_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_man_gain(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_manual_gain_cmd *cp = cmd;

	LOG_DBG("AICS set manual gain mode");

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_manual_gain_set(aics_client_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}

	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
			if (bt_aics_manual_gain_set(aics_server_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_auto_gain(const void *cmd, uint16_t cmd_len,
			      void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_auto_gain_cmd *cp = cmd;

	LOG_DBG("AICS set automatic gain mode");

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_automatic_gain_set(aics_client_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}

	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
			if (bt_aics_automatic_gain_set(aics_server_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_man_gain_only(const void *cmd, uint16_t cmd_len,
				  void *rsp, uint16_t *rsp_len)
{
	LOG_DBG("AICS manual gain only set");

	for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
		if (bt_aics_gain_set_manual_only(aics_server_instance.aics[i]) != 0) {
			return BTP_STATUS_FAILED;
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_auto_gain_only(const void *cmd, uint16_t cmd_len,
				   void *rsp, uint16_t *rsp_len)
{
	LOG_DBG("AICS auto gain only set");

	for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
		if (bt_aics_gain_set_auto_only(aics_server_instance.aics[i]) != 0) {
			return BTP_STATUS_FAILED;
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_mute_disable(const void *cmd, uint16_t cmd_len,
				 void *rsp, uint16_t *rsp_len)
{
	LOG_DBG("AICS disable mute");

	for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
		if (bt_aics_disable_mute(aics_server_instance.aics[i]) != 0) {
			return BTP_STATUS_FAILED;
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_desc_set(const void *cmd, uint16_t cmd_len,
			     void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_audio_desc_cmd *cp = cmd;
	char description[BT_AICS_MAX_INPUT_DESCRIPTION_SIZE];

	LOG_DBG("AICS set description");

	if (cmd_len < sizeof(*cp) ||
	    cmd_len != sizeof(*cp) + cp->desc_len) {
		return BTP_STATUS_FAILED;
	}

	if (cp->desc_len >= sizeof(description)) {
		return BTP_STATUS_FAILED;
	}

	memcpy(description, cp->desc, cp->desc_len);
	description[cp->desc_len] = '\0';

	for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
		if (bt_aics_description_set(aics_server_instance.aics[i], description) != 0) {
			return BTP_STATUS_FAILED;
		}
	}

	return BTP_STATUS_SUCCESS;
}

static uint8_t aics_desc(const void *cmd, uint16_t cmd_len,
			 void *rsp, uint16_t *rsp_len)
{
	const struct btp_aics_desc_cmd *cp = cmd;

	LOG_DBG("AICS Description");

	if (!bt_addr_le_eq(&cp->address, BT_ADDR_LE_ANY)) {
		for (uint8_t i = 0; i < aics_client_instance.aics_cnt; i++) {
			if (bt_aics_description_get(aics_client_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	} else {
		for (uint8_t i = 0; i < aics_server_instance.aics_cnt; i++) {
			if (bt_aics_description_get(aics_server_instance.aics[i]) != 0) {
				return BTP_STATUS_FAILED;
			}
		}
	}

	return BTP_STATUS_SUCCESS;
}

static const struct btp_handler aics_handlers[] = {
	{
		.opcode = BTP_AICS_READ_SUPPORTED_COMMANDS,
		.index = BTP_INDEX_NONE,
		.expect_len = 0,
		.func = aics_supported_commands,
	},
	{
		.opcode = BTP_AICS_SET_GAIN,
		.expect_len = sizeof(struct btp_aics_set_gain_cmd),
		.func = aics_set_gain,
	},
	{
		.opcode = BTP_AICS_MUTE,
		.expect_len = sizeof(struct btp_aics_mute_cmd),
		.func = aics_mute,
	},
	{
		.opcode = BTP_AICS_UNMUTE,
		.expect_len = sizeof(struct btp_aics_unmute_cmd),
		.func = aics_unmute,
	},
	{
		.opcode = BTP_AICS_GAIN_SETTING_PROP,
		.expect_len = sizeof(struct btp_aics_gain_setting_prop_cmd),
		.func = aics_gain_setting_prop,
	},
	{
		.opcode = BTP_AICS_MUTE_DISABLE,
		.expect_len = 0,
		.func = aics_mute_disable,
	},
	{
		.opcode = BTP_AICS_MAN_GAIN,
		.expect_len = sizeof(struct btp_aics_manual_gain_cmd),
		.func = aics_man_gain,
	},
	{
		.opcode = BTP_AICS_AUTO_GAIN,
		.expect_len = sizeof(struct btp_aics_auto_gain_cmd),
		.func = aics_auto_gain,
	},
	{
		.opcode = BTP_AICS_AUTO_GAIN_ONLY,
		.expect_len = 0,
		.func = aics_auto_gain_only,
	},
	{
		.opcode = BTP_AICS_MAN_GAIN_ONLY,
		.expect_len = 0,
		.func = aics_man_gain_only,
	},
	{
		.opcode = BTP_AICS_DESCRIPTION_SET,
		.expect_len = BTP_HANDLER_LENGTH_VARIABLE,
		.func = aics_desc_set,
	},
	{
		.opcode = BTP_AICS_DESCRIPTION,
		.expect_len = sizeof(struct btp_aics_desc_cmd),
		.func = aics_desc,
	},
	{
		.opcode = BTP_AICS_TYPE,
		.expect_len = sizeof(struct btp_aics_type_cmd),
		.func = aics_type,
	},
	{
		.opcode = BTP_AICS_STATUS,
		.expect_len = sizeof(struct btp_aics_status_cmd),
		.func = aics_status,
	},
	{
		.opcode = BTP_AICS_STATE,
		.expect_len = sizeof(struct btp_aics_state_cmd),
		.func = aics_state,
	},
};

static void aics_state_cb(struct bt_aics *inst, int err, int8_t gain,
			  uint8_t mute, uint8_t mode)
{
	struct bt_conn *conn;

	bt_aics_client_conn_get(inst, &conn);
	btp_send_aics_state_ev(conn, gain, mute, mode);

	LOG_DBG("AICS state callback (%d)", err);
}

static void aics_gain_setting_cb(struct bt_aics *inst, int err, uint8_t units,
				 int8_t minimum, int8_t maximum)
{
	struct bt_conn *conn;

	bt_aics_client_conn_get(inst, &conn);
	btp_send_gain_setting_properties_ev(conn, units, minimum, maximum);

	LOG_DBG("AICS gain setting callback (%d)", err);
}

static void aics_input_type_cb(struct bt_aics *inst, int err,
			       uint8_t input_type)
{
	struct bt_conn *conn;

	bt_aics_client_conn_get(inst, &conn);
	btp_send_aics_input_type_event(conn, input_type);

	LOG_DBG("AICS input type callback (%d)", err);
}

static void aics_status_cb(struct bt_aics *inst, int err, bool active)
{
	struct bt_conn *conn;

	bt_aics_client_conn_get(inst, &conn);
	btp_aics_status_ev(conn, active);

	LOG_DBG("AICS status callback (%d)", err);
}

static void aics_description_cb(struct bt_aics *inst, int err,
				char *description)
{
	struct bt_conn *conn;
	uint8_t data_len = strlen(description);

	bt_aics_client_conn_get(inst, &conn);
	btp_aics_description_ev(conn, data_len, description);

	LOG_DBG("AICS description callback (%d)", err);
}

struct bt_aics_cb aics_client_cb = {
	.state = aics_state_cb,
	.gain_setting = aics_gain_setting_cb,
	.type = aics_input_type_cb,
	.status = aics_status_cb,
	.description = aics_description_cb,
};

uint8_t tester_init_aics(void)
{
	tester_register_command_handlers(BTP_SERVICE_ID_AICS, aics_handlers,
					 ARRAY_SIZE(aics_handlers));

	return tester_init_vcs();
}

uint8_t tester_unregister_aics(void)
{
	return BTP_STATUS_SUCCESS;
}
