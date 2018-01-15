/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 version	1.0 	mayh
 */

#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/irqreturn.h>
#include <linux/kd.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <asm/irq.h>

#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
#include <linux/regulator/consumer.h>
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/regmap.h>
#include "msm_dba_internal.h"
#include <linux/mdss_io_util.h>

//#define LT8912_DEBUG

#ifdef LT8912_DEBUG
#define lt8912_debug(fmt, args...) printk(KERN_INFO fmt, ##args)
#else
#define lt8912_debug(fmt, args...)
#endif

/*
 *Because there is no a valid interrup pin.
 *So polling read cbus register to check hotplug.
 *Todo
*/
#define HOTPLUG_POLLING

#define CHECK_HOTPLUG_TIME 2000

#define VREG_1V8 "vreg_1v8"
#define VREG_3V3 "vreg_3v3"
#define VREG_5V "vreg_5v"

#define EDID_SEG_SIZE 0x100

#define device_name	"lt8912_dba"

static struct i2c_client *hdmi_i2c_client = NULL;
/**
 * struct lt8912_data - Cached chip configuration data
 * @client: I2C client
 * @dev: device structure
 * @input_hotplug: hotplug input device structure
 * @hotplug_work: hotplug work structure
 *
 */
struct lt8912_data {
	struct i2c_client *lt8912_client;
	struct regmap			*regmap;
	struct input_dev		*input_hotplug;
	struct delayed_work hotplug_work;
	int reset_gpio;
	struct regulator *vreg_3v3;
	struct regulator *vreg_1v8;
	struct regulator *vreg_5v;
	int last_status;
	struct msm_dba_device_info dba_dev_info;
	u8 edid_buf[EDID_SEG_SIZE];
	int edid_size;
	int is_power_on;
	struct msm_dba_video_cfg video_cfg;
};

static int read_edid_raw_data(char *buf, int size)
{
	int ret = 0;
	unsigned char regaddr = 0;
	struct i2c_adapter *adp;
	struct i2c_msg msg[2];

	if(hdmi_i2c_client == NULL)
		return -ENODEV;

	adp = hdmi_i2c_client->adapter;

	msg[0].addr	= 0x50,
	msg[0].flags= 0;
	msg[0].len	= 1;
	msg[0].buf	= &regaddr;
	msg[1].addr	= 0x50,
	msg[1].flags= I2C_M_RD;
	msg[1].len	= size;
	msg[1].buf	= buf;

	ret = i2c_transfer(adp, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		return -EIO;
	}
	return ret;
}

static int lt8912_i2c_read_byte(struct lt8912_data *data,
							unsigned int reg)
{
	int rc = 0;
	int val = 0;

	rc = regmap_read(data->regmap, reg, &val);
	if (rc) {
		dev_err(&data->lt8912_client->dev, "read 0x%x failed.(%d)\n",
				reg, rc);
		return rc;
	}
	dev_dbg(&data->lt8912_client->dev, "read 0x%x value = 0x%x\n",
				reg, val);
	return val;
}

static int lt8912_i2c_write_byte(struct lt8912_data *data,
					unsigned int reg, unsigned int val)
{
	int rc = 0;

	rc = regmap_write(data->regmap, reg, val);
	if (rc) {
		dev_err(&data->lt8912_client->dev,
				"write 0x%x register failed\n", reg);
		return rc;
	}

	return 0;
}

static const struct of_device_id of_rk_lt8912_match[] = {
	{ .compatible = "lontium,lt8912_dba"},
	{  },
};

static const struct i2c_device_id lt8912_id[] = {
	{device_name, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, lt8912_id);

static void lt8912_notify_clients(struct msm_dba_device_info *dev,
		enum msm_dba_callback_event event)
{
	struct msm_dba_client_info *c;
	struct list_head *pos = NULL;

	if (!dev) {
		pr_err("%s: invalid input\n", __func__);
		return;
	}

	list_for_each(pos, &dev->client_list) {
		c = list_entry(pos, struct msm_dba_client_info, list);

		pr_debug("%s: notifying event %d to client %s\n", __func__,
			event, c->client_name);

		if (c && c->cb)
			c->cb(c->cb_data, event);
	}
}

static int lt8912_check_hotplug(struct lt8912_data *data)
{
	int ret = 0;
	int val = 0;
	int connected = 0;
	ktime_t timestamp;

	timestamp = ktime_get_boottime();

	data->lt8912_client->addr = 0x48;
	ret = lt8912_i2c_read_byte(data, 0xc1);

	if(ret < 0)
	{
		dev_err(&data->lt8912_client->dev, "read 0xc1 failed\n");
		return ret;
	}
	val = ret;

	if (val != data->last_status)
	{
		input_report_abs(data->input_hotplug, ABS_MISC, val);
		input_event(data->input_hotplug, EV_SYN, SYN_CONFIG,
					ktime_to_timespec(timestamp).tv_sec);
		input_event(data->input_hotplug, EV_SYN, SYN_CONFIG,
					ktime_to_timespec(timestamp).tv_nsec);
		input_sync(data->input_hotplug);

		dev_dbg(&data->lt8912_client->dev,
				"input report val = %d\n", val);

		/*check connect or disconnect*/
		if(val & 0x80)
		{
			lt8912_debug("lt8912 connect\n");

			memset(data->edid_buf, 0x0, ARRAY_SIZE(data->edid_buf));
			data->edid_size = 0;

			ret = read_edid_raw_data(data->edid_buf, ARRAY_SIZE(data->edid_buf));
			if(ret < 0)
			{
				dev_err(&data->lt8912_client->dev, "read edid failed\n");
				data->edid_size = 0;
			}
			else
				data->edid_size = ret;

			lt8912_notify_clients(&data->dba_dev_info, MSM_DBA_CB_HPD_CONNECT);
			connected = 1;
		}
		else
		{
			lt8912_debug("lt8912 disconnect\n");
			lt8912_notify_clients(&data->dba_dev_info, MSM_DBA_CB_HPD_DISCONNECT);
			connected = 0;
		}
	}
	data->last_status = val;

	return connected;
}

static void lt8912_input_work_fn(struct work_struct *work)
{
	struct lt8912_data *data;

	data = container_of((struct delayed_work *)work,
					struct lt8912_data, hotplug_work);

	lt8912_check_hotplug(data);

	#ifdef HOTPLUG_POLLING
	schedule_delayed_work(&data->hotplug_work,
						  msecs_to_jiffies(CHECK_HOTPLUG_TIME));
	#endif
}

static int digital_clock_enable(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x48;
	rc = lt8912_i2c_write_byte(data, 0x08, 0xff);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x09, 0xff);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x0a, 0xff);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x0b, 0xff);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x0c, 0xff);
	if (rc)
		return rc;

	return 0;
}

static int tx_analog(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x48;
	rc = lt8912_i2c_write_byte(data, 0x31, 0xa1);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x32, 0xa1);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x33, 0x03);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x37, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x38, 0x22);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x60, 0x82);
	if (rc)
		return rc;
	return 0;
}

static int cbus_analog(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x48;
	rc = lt8912_i2c_write_byte(data, 0x39, 0x45);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3b, 0x00);
	if (rc)
		return rc;

	return 0;
}

static int hdmi_pll_analog(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x48;
	rc = lt8912_i2c_write_byte(data, 0x44, 0x31);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x55, 0x44);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x57, 0x01);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x5a, 0x02);
	if (rc)
		return rc;
	return 0;
}

static int mipi_rx_logic_res(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x48;
	rc = lt8912_i2c_write_byte(data, 0x03, 0x7f);
	if (rc)
		return rc;

	msleep(100);

	rc = lt8912_i2c_write_byte(data, 0x03, 0xff);
	if (rc)
		return rc;

	return 0;
}

static int mipi_basic_set(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x49;

	/* term en	To analog phy for trans lp mode to hs mode */
	rc = lt8912_i2c_write_byte(data, 0x10, 0x20);
	if (rc)
		return rc;

	/* settle Set timing for dphy trans state from PRPR to SOT state */
	rc = lt8912_i2c_write_byte(data, 0x11, 0x04);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x12, 0x04);
	if (rc)
		return rc;

	/*00 4 lane, 01 1 lane, 02 2 lane, 03 3lane */
	rc = lt8912_i2c_write_byte(data, 0x13, data->video_cfg.num_of_input_lanes % 0x04);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x14, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x15, 0x00);
	if (rc)
		return rc;

	/* hshift 3 */
	rc = lt8912_i2c_write_byte(data, 0x1a, 0x03);
	if (rc)
		return rc;

	/* vshift 3 */
	rc = lt8912_i2c_write_byte(data, 0x1b, 0x03);
	if (rc)
		return rc;
	return 0;
}

static int mipi_timing_set(struct lt8912_data *data)
{
	int ret = 0;
	struct msm_dba_video_cfg *p_video_cfg = &data->video_cfg;
	data->lt8912_client->addr = 0x49;


	lt8912_i2c_write_byte(data, 0x18, (p_video_cfg->h_pulse_width%256)); // hwidth
	lt8912_i2c_write_byte(data, 0x19, (p_video_cfg->v_pulse_width%256)); // vwidth

	lt8912_i2c_write_byte(data, 0x1c, (p_video_cfg->h_active%256)); // H_active[7:0]
	lt8912_i2c_write_byte(data, 0x1d, (p_video_cfg->h_active/256)); // H_active[15:8]

	lt8912_i2c_write_byte(data, 0x1e,0x67); // hs/vs/de pol hdmi sel pll sel
	lt8912_i2c_write_byte(data, 0x2f,0x0c); // fifo_buff_length 12

	lt8912_i2c_write_byte(data, 0x34, ((p_video_cfg->h_active + p_video_cfg->h_back_porch +
				p_video_cfg->h_front_porch + p_video_cfg->h_pulse_width)%256)); // H_total[7:0]
	lt8912_i2c_write_byte(data, 0x35, ((p_video_cfg->h_active + p_video_cfg->h_back_porch +
				p_video_cfg->h_front_porch + p_video_cfg->h_pulse_width)/256)); // H_total[15:8]

	lt8912_i2c_write_byte(data, 0x36, ((p_video_cfg->v_active + p_video_cfg->v_back_porch +
				p_video_cfg->v_front_porch + p_video_cfg->v_pulse_width)%256)); // V_total[7:0]
	lt8912_i2c_write_byte(data, 0x37, ((p_video_cfg->v_active + p_video_cfg->v_back_porch +
				p_video_cfg->v_front_porch + p_video_cfg->v_pulse_width)/256)); // V_total[15:8]

	lt8912_i2c_write_byte(data, 0x38, (p_video_cfg->v_back_porch%256)); // VBP[7:0]
	lt8912_i2c_write_byte(data, 0x39, (p_video_cfg->v_back_porch/256)); // VBP[15:8]

	lt8912_i2c_write_byte(data, 0x3a, (p_video_cfg->v_front_porch%256)); // VFP[7:0]
	lt8912_i2c_write_byte(data, 0x3b, (p_video_cfg->v_front_porch/256)); // VFP[15:8]

	lt8912_i2c_write_byte(data, 0x3c, (p_video_cfg->h_back_porch%256)); // HBP[7:0]
	lt8912_i2c_write_byte(data, 0x3d, (p_video_cfg->h_back_porch/256)); // HBP[15:8]

	lt8912_i2c_write_byte(data, 0x3e, (p_video_cfg->h_front_porch%256)); // HFP[7:0]
	lt8912_i2c_write_byte(data, 0x3f, (p_video_cfg->h_front_porch/256)); // HFP[15:8]

	return ret;
}

static int lt8912_standby(struct lt8912_data *data)
{
	int ret = 0;
	data->lt8912_client->addr = 0x48;

	ret = lt8912_i2c_write_byte(data, 0x08,0x00);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x09,0x81);
		if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x0a,0x00);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x0b,0x20);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x0c,0x00);
	if (ret)
		return ret;

	ret = lt8912_i2c_write_byte(data, 0x54,0x1d);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x51,0x15);
	if (ret)
		return ret;

	ret = lt8912_i2c_write_byte(data, 0x44,0x31);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x41,0xbd);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x5c,0x11);
	if (ret)
		return ret;

	ret = lt8912_i2c_write_byte(data, 0x30,0x08);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x31,0x00);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x32,0x00);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x33,0x00);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x34,0x00);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x35,0x00);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x36,0x00);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x37,0x00);
	if (ret)
		return ret;
	ret = lt8912_i2c_write_byte(data, 0x38,0x00);
	if (ret)
		return ret;
	return ret;
}

#ifdef MIPI_1080P
static int mipi_dig_1080p(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x49;
	rc = lt8912_i2c_write_byte(data, 0x18, 0x2c);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x19, 0x05);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x1c, 0x80);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x1d, 0x07);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x2f, 0x0c);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x34, 0x98);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x35, 0x08);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x36, 0x65);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x37, 0x04);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x38, 0x24);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x39, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3a, 0x04);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3b, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3c, 0x94);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3d, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3e, 0x58);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3f, 0x00);
	if (rc)
		return rc;
	return 0;
}

#endif

#ifdef MIPI_720P
static int mipi_dig_720p(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x49;
	rc = lt8912_i2c_write_byte(data, 0x18, 0x28);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x19, 0x05);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x1c, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x1d, 0x05);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x1e, 0x67);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x2f, 0x0c);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x34, 0x72);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x35, 0x06);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x36, 0xee);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x37, 0x02);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x38, 0x14);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x39, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3a, 0x05);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3b, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3c, 0xdc);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3d, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3e, 0x6e);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3f, 0x00);
	if (rc)
		return rc;
	return 0;
}

#endif

#ifdef MIPI_480P
static int mipi_dig_480p(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x49;
	rc = lt8912_i2c_write_byte(data, 0x18, 0x60);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x19, 0x02);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x1c, 0x80);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x1d, 0x02);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x1e, 0x67);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x2f, 0x0c);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x34, 0x20);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x35, 0x03);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x36, 0x0d);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x37, 0x02);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x38, 0x20);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x39, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3a, 0x0a);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3b, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3c, 0x30);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3d, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3e, 0x10);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x3f, 0x00);
	if (rc)
		return rc;
	return 0;
}
#endif

static int dds_config(struct lt8912_data *data)
{
	int rc = 0;

	data->lt8912_client->addr = 0x49;

	/* strm_sw_freq_word[ 7: 0] */
	rc = lt8912_i2c_write_byte(data, 0x4e, 0x6A);
	if (rc)
		return rc;

	/* strm_sw_freq_word[15: 8] */
	rc = lt8912_i2c_write_byte(data, 0x4f, 0x4D);
	if (rc)
		return rc;

	/* strm_sw_freq_word[23:16] */
	rc = lt8912_i2c_write_byte(data, 0x50, 0xF3);
	if (rc)
		return rc;

	/* [0]=strm_sw_freq_word[24]//[7]=strm_sw_freq_word_en=0,
	[6]=strm_err_clr=0 */
	rc = lt8912_i2c_write_byte(data, 0x51, 0x80);
	if (rc)
		return rc;

	/* full_value  464 */
	rc = lt8912_i2c_write_byte(data, 0x1f, 0x90);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x20, 0x01);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x21, 0x68);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x22, 0x01);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x23, 0x5E);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x24, 0x01);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x25, 0x54);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x26, 0x01);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x27, 0x90);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x28, 0x01);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x29, 0x68);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x2a, 0x01);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x2b, 0x5E);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x2c, 0x01);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x2d, 0x54);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x2e, 0x01);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x42, 0x64);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x43, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x44, 0x04);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x45, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x46, 0x59);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x47, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x48, 0xf2);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x49, 0x06);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x4a, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x4b, 0x72);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x4c, 0x45);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x4d, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x52, 0x08);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x53, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x54, 0xb2);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x55, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x56, 0xe4);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x57, 0x0d);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x58, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x59, 0xe4);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x5a, 0x8a);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x5b, 0x00);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x5c, 0x34);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x1e, 0x4f);
	if (rc)
		return rc;
	rc = lt8912_i2c_write_byte(data, 0x51, 0x00);
	if (rc)
		return rc;
	return 0;
}

static int lt8912_reset(struct lt8912_data *pdata)
{
	if(gpio_is_valid(pdata->reset_gpio))
	{
		gpio_set_value(pdata->reset_gpio, 0);
		msleep(20);
		gpio_set_value(pdata->reset_gpio, 1);
		msleep(20);
	}

	return 0;
}

static int lt8912_set_power(struct lt8912_data *pdata, int on)
{
	int ret = 0;
	if((on && pdata->is_power_on) || (!on && !pdata->is_power_on))
		return 0;

	if(on)
	{
		if(pdata->vreg_5v)
		{
			ret = regulator_enable(pdata->vreg_5v);
			if(ret)
			{
				dev_err(&pdata->lt8912_client->dev, "enable 5v failed. ret=%d\n", ret);
				goto out;
			}
		}

		if(pdata->vreg_3v3)
		{
			ret = regulator_enable(pdata->vreg_3v3);
			if(ret)
			{
				dev_err(&pdata->lt8912_client->dev, "enable 3.3v failed. ret=%d\n", ret);
				goto err_en_3v3;
			}
		}

		if(pdata->vreg_1v8)
		{
			ret = regulator_enable(pdata->vreg_1v8);
			if(ret)
			{
				dev_err(&pdata->lt8912_client->dev, "enable 1.8v failed. ret=%d\n", ret);
				goto err_en_1v8;
			}
		}

		#ifdef HOTPLUG_POLLING
		/*reset*/
		lt8912_reset(pdata);

		ret = digital_clock_enable(pdata);
		if(ret)
		{
			dev_err(&pdata->lt8912_client->dev, "digital_clock_enable failed\n");
			goto err_init_register;
		}

		ret = tx_analog(pdata);
		if(ret)
		{
			pr_err("%s: tx_analog failed\n", __func__);
			goto err_init_register;
		}

		/*enable cbus. so we can check if HDMI is inserted by cbus register*/
		ret = cbus_analog(pdata);
		if(ret)
		{
			dev_err(&pdata->lt8912_client->dev, "cbus analog failed. ret=%d\n", ret);
			goto err_init_register;
		}

		lt8912_standby(pdata);

		schedule_delayed_work(&pdata->hotplug_work, 0);
		#endif

		pdata->is_power_on = 1;
	}
	else if(!on)
	{
		cancel_delayed_work_sync(&pdata->hotplug_work);
		if(pdata->vreg_5v)
		{
			ret = regulator_disable(pdata->vreg_5v);
			if(ret)
				dev_err(&pdata->lt8912_client->dev, "disable 5v failed. ret=%d\n", ret);
		}

		if(pdata->vreg_3v3)
		{
			ret = regulator_disable(pdata->vreg_3v3);
			if(ret)
				dev_err(&pdata->lt8912_client->dev, "disable 3.3v failed. ret=%d\n", ret);
		}

		if(pdata->vreg_1v8)
		{
			ret = regulator_disable(pdata->vreg_1v8);
			if(ret)
				dev_err(&pdata->lt8912_client->dev, "disable 1.8v failed. ret=%d\n", ret);
		}

		if(gpio_is_valid(pdata->reset_gpio))
			gpio_set_value(pdata->reset_gpio, 0);

		pdata->is_power_on = 0;
	}

	return 0;
err_init_register:
	gpio_set_value(pdata->reset_gpio, 0);
err_en_1v8:
	if(pdata->vreg_3v3)
		regulator_disable(pdata->vreg_3v3);
err_en_3v3:
	if(pdata->vreg_5v)
		regulator_disable(pdata->vreg_5v);
out:
	return ret;
}

static int lt8912_init_input(struct lt8912_data *data)
{
	struct input_dev *input;
	int status;

	input = devm_input_allocate_device(&data->lt8912_client->dev);
	if (!input) {
		dev_err(&data->lt8912_client->dev,
			"allocate light input device failed\n");
		return PTR_ERR(input);
	}

	input->name = "lt8912";
	input->phys = "lt8912/input0";
	input->id.bustype = BUS_I2C;

	__set_bit(EV_ABS, input->evbit);
	input_set_abs_params(input, ABS_MISC, 0, 655360, 0, 0);

	status = input_register_device(input);
	if (status) {
		input_free_device(input);
		dev_err(&data->lt8912_client->dev,
			"register light input device failed.\n");
		return status;
	}

	data->input_hotplug = input;
	return 0;
}

static int lt8912_parse_dt(struct device *dev, struct lt8912_data *data)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	data->reset_gpio = of_get_named_gpio(np, "qcom,hdmi-reset", 0);
	if(!gpio_is_valid(data->reset_gpio))
		dev_err(dev, "get reset gpio failed\n");

	if(of_get_property(np, VREG_1V8"-supply", NULL))
	{
		data->vreg_1v8 = regulator_get(dev, VREG_1V8);
		if(IS_ERR(data->vreg_1v8))
		{
			ret = PTR_ERR(data->vreg_1v8);
			dev_err(dev, "vreg_1v8 get failed. erro=%d\n", ret);
			data->vreg_1v8 = NULL;
			goto out;
		}
	}

	if(of_get_property(np, VREG_3V3"-supply", NULL))
	{
		data->vreg_3v3 = regulator_get(dev, VREG_3V3);
		if(IS_ERR(data->vreg_3v3))
		{
			ret = PTR_ERR(data->vreg_3v3);
			dev_err(dev, "vreg_3v3 get failed. erro=%d\n", ret);
			data->vreg_3v3 = NULL;
			goto err_3v3;
		}
	}

	if(of_get_property(np, VREG_5V"-supply", NULL))
	{
		data->vreg_5v = regulator_get(dev, VREG_5V);
		if(IS_ERR(data->vreg_5v))
		{
			ret = PTR_ERR(data->vreg_5v);
			dev_err(dev, "vreg_5v get failed. erro=%d\n", ret);
			data->vreg_5v = NULL;
			goto err_5v;
		}
	}

	return 0;
err_5v:
	if(data->vreg_3v3)
	{
		regulator_put(data->vreg_3v3);
		data->vreg_3v3 = NULL;
	}
err_3v3:
	if(data->vreg_1v8)
	{
		regulator_put(data->vreg_1v8);
		data->vreg_1v8 = NULL;
	}

out:
	return ret;
}

static struct lt8912_data *lt8912_dba_get_platform_data(void *client)
{
	struct lt8912_data *pdata = NULL;
	struct msm_dba_device_info *dev;
	struct msm_dba_client_info *cinfo =
		(struct msm_dba_client_info *)client;

	if (!cinfo) {
		pr_err("%s: invalid client data\n", __func__);
		goto end;
	}

	dev = cinfo->dev;
	if (!dev) {
		pr_err("%s: invalid device data\n", __func__);
		goto end;
	}

	pdata = container_of(dev, struct lt8912_data, dba_dev_info);
	if (!pdata)
		pr_err("%s: invalid platform data\n", __func__);

end:
	return pdata;
}

static int lt8912_dba_get_edid_size(void *client, u32 *size, u32 flags)
{
	int ret = 0;
	struct lt8912_data *pdata =
		lt8912_dba_get_platform_data(client);

	if (!pdata) {
		pr_err("%s: invalid platform data\n", __func__);
		return ret;
	}

	if (!size) {
		ret = -EINVAL;
		goto end;
	}

	*size = pdata->edid_size;
end:
	return ret;
}

static int lt8912_dba_get_raw_edid(void *client,
	u32 size, char *buf, u32 flags)
{
	struct lt8912_data *pdata =
		lt8912_dba_get_platform_data(client);

	lt8912_debug("enter %s, size=%d, flags=%d\n", __func__, size, flags);
	if (!pdata || !buf) {
		pr_err("%s: invalid data\n", __func__);
		goto end;
	}

	size = min_t(u32, size, sizeof(pdata->edid_buf));

	memcpy(buf, pdata->edid_buf, size);
end:
	return 0;
}

static int lt8912_dba_check_hpd(void *client, u32 flags)
{
	struct lt8912_data *pdata = lt8912_dba_get_platform_data(client);
	lt8912_debug("enter %s, flags=%d\n", __func__, flags);
	if (!pdata) {
		pr_err("%s: invalid platform data\n", __func__);
		return -EINVAL;
	}

	return lt8912_check_hotplug(pdata);
}

static int lt8912_dba_power_on(void *client, bool on, u32 flags)
{
	int ret = 0;
	struct lt8912_data *pdata = lt8912_dba_get_platform_data(client);

	lt8912_debug("enter %s, on-%d\n", __func__, on);
	if (!pdata) {
		pr_err("%s: invalid platform data\n", __func__);
		return ret;
	}

	ret = lt8912_set_power(pdata, on);
	if(ret)
		pr_err("%s: set power %d failed\n", __func__, on);
	return ret;
}

static int lt8912_dba_video_on(void *client, bool on,
			struct msm_dba_video_cfg *cfg, u32 flags)
{
	int ret = 0;
	struct lt8912_data *pdata = lt8912_dba_get_platform_data(client);

	dev_dbg(&pdata->lt8912_client->dev, "%s: on-%d\n", __func__, on);
	dev_dbg(&pdata->lt8912_client->dev, "video_cfg: h_active=%d, h_front_porch=%d, h_pulse_width=%d, h_back_porch=%d, \
		h_polarity=%d, v_active=%d, v_front_porch=%d, v_pulse_width=%d, v_back_porch=%d,\
		v_polarity=%d, pclk_khz=%d, interlaced=%d, num_of_input_lanes=%d\n",
		pdata->video_cfg.h_active, pdata->video_cfg.h_front_porch, pdata->video_cfg.h_pulse_width, pdata->video_cfg.h_back_porch,
		pdata->video_cfg.h_polarity, pdata->video_cfg.v_active, pdata->video_cfg.v_front_porch, pdata->video_cfg.v_pulse_width, pdata->video_cfg.v_back_porch,
		pdata->video_cfg.v_polarity, pdata->video_cfg.pclk_khz, pdata->video_cfg.interlaced, pdata->video_cfg.num_of_input_lanes);

	if(on)
	{
		if(cfg == NULL)
			return -EINVAL;

		memcpy(&pdata->video_cfg, cfg, sizeof(struct msm_dba_video_cfg));

		#ifdef HOTPLUG_POLLING
		/*must stop polling check hotplug. Because we must do once reset.*/
		cancel_delayed_work_sync(&pdata->hotplug_work);
		#endif

		//reset once. must do it
		lt8912_reset(pdata);

		ret = digital_clock_enable(pdata);
		if(ret)
		{
			pr_err("%s: digital clock enable failed\n", __func__);
			goto out;
		}

		ret = tx_analog(pdata);
		if(ret)
		{
			pr_err("%s: tx_analog failed\n", __func__);
			goto out;
		}

		/*reenable cbus. so we can check hotplug*/
		ret = cbus_analog(pdata);
		if(ret)
		{
			pr_err("%s: cbus_analog failed\n", __func__);
			goto out;
		}

		ret = hdmi_pll_analog(pdata);
		if(ret)
		{
			pr_err("%s: hdmi_pll_analog failed\n", __func__);
			goto out;
		}

		ret = mipi_basic_set(pdata);
		if(ret)
		{
			pr_err("%s: mipi_basic_set failed\n", __func__);
			goto out;
		}

		ret = mipi_timing_set(pdata);
		if(ret)
		{
			pr_err("%s: mipi_timing_set failed\n", __func__);
			goto out;
		}

		ret = dds_config(pdata);
		if(ret)
		{
			pr_err("%s: dds_config failed\n", __func__);
			goto out;
		}

		ret = mipi_rx_logic_res(pdata);
		if(ret)
		{
			pr_err("%s: mipi_rx_logic_res failed\n", __func__);
			goto out;
		}

	}
	else
	{
		ret = lt8912_standby(pdata);
		if(ret)
			pr_err("%s:standby failed\n", __func__);
	}

out:
	/*polling hotplug status again*/
	#ifdef HOTPLUG_POLLING
	if(on)
		schedule_delayed_work(&pdata->hotplug_work, 0);
	#endif

	return ret;
}

static int lt8912_register_dba(struct lt8912_data *pdata)
{
	struct msm_dba_ops *client_ops;
	struct msm_dba_device_ops *dev_ops;

	if (!pdata)
		return -EINVAL;

	client_ops = &pdata->dba_dev_info.client_ops;
	dev_ops = &pdata->dba_dev_info.dev_ops;

	client_ops->get_edid_size	= lt8912_dba_get_edid_size;
	client_ops->get_raw_edid	= lt8912_dba_get_raw_edid;
	client_ops->check_hpd = lt8912_dba_check_hpd;
	client_ops->power_on = lt8912_dba_power_on;
	client_ops->video_on = lt8912_dba_video_on;

	strlcpy(pdata->dba_dev_info.chip_name, "lt8912",
		sizeof(pdata->dba_dev_info.chip_name));

	pdata->dba_dev_info.instance_id = 0;

	mutex_init(&pdata->dba_dev_info.dev_mutex);

	INIT_LIST_HEAD(&pdata->dba_dev_info.client_list);

	return msm_dba_add_probed_device(&pdata->dba_dev_info);
}


static ssize_t lt8912_register_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lt8912_data *data = dev_get_drvdata(dev);
	unsigned int val = 0;
	int i = 0;
	ssize_t count = 0;

	val = lt8912_i2c_read_byte(data, 0x00);

	count += snprintf(&buf[count], PAGE_SIZE,
					  "0x%x: 0x%x\n", 0x00, val);
	val = lt8912_i2c_read_byte(data, 0x01);

	count += snprintf(&buf[count], PAGE_SIZE,
					  "0x%x: 0x%x\n", 0x01, (char)val);

	val = lt8912_i2c_read_byte(data, 0x9c);

	count += snprintf(&buf[count], PAGE_SIZE,
					  "0x%x: 0x%x\n", 0x9c, (char)val);

	val = lt8912_i2c_read_byte(data, 0x9d);

	count += snprintf(&buf[count], PAGE_SIZE,
					  "0x%x: 0x%x\n", 0x9d, (char)val);

	val = lt8912_i2c_read_byte(data, 0x9e);

	count += snprintf(&buf[count], PAGE_SIZE,
					  "0x%x: 0x%x\n", 0x9e, (char)val);

	val = lt8912_i2c_read_byte(data, 0x9f);

	count += snprintf(&buf[count], PAGE_SIZE,
					  "0x%x: 0x%x\n", 0x9f, (char)val);

	data->lt8912_client->addr = 0x49;
	for (i = 0x00; i <= 0x60; i++) {
		val = lt8912_i2c_read_byte(data, i);

		count += snprintf(&buf[count], PAGE_SIZE,
						  "0x%x: 0x%x\n", i, val);
	}
	data->lt8912_client->addr = 0x48;

	return count;
}

static DEVICE_ATTR(register, S_IWUSR | S_IRUGO,
				   lt8912_register_show,
				   NULL);
static struct attribute *lt8912_attr[] = {
	&dev_attr_register.attr,
	NULL
};

static const struct attribute_group lt8912_attr_group = {
	.attrs = lt8912_attr,
};

static struct regmap_config lt8912_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int lontium_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct lt8912_data *data;
	int ret;

	lt8912_debug("enter %s\n", __func__);
	if(hdmi_i2c_client == NULL)
	{
		pr_err("%s no hdmi i2c\n", __func__);
		return -EPROBE_DEFER;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "lt8912 i2c check failed.\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(struct lt8912_data),
						GFP_KERNEL);

	if(IS_ERR(data))
	{
		dev_err(&client->dev, "no memory\n");
		return PTR_ERR(data);
	}

	if (client->dev.of_node) {
		ret = lt8912_parse_dt(&client->dev, data);
		if (ret) {
			dev_err(&client->dev,
				"unable to parse device tree.(%d)\n", ret);
			goto out;
		}
	} else {
		dev_err(&client->dev, "device tree not found.\n");
		ret = -ENODEV;
		goto out;
	}

	if (gpio_is_valid(data->reset_gpio)) {
		ret = gpio_request(data->reset_gpio, "lt8912_reset_gpio");
		if (ret) {
			dev_err(&client->dev, "reset gpio request failed");
			goto err_req_rest_gpio;
		}

		ret = gpio_direction_output(data->reset_gpio, 0);
		if (ret) {
			dev_err(&client->dev,
				"set_direction for reset gpio failed\n");
			goto err_reset_gpio_out;
		}
	}

	data->regmap = devm_regmap_init_i2c(client, &lt8912_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "init regmap failed.(%ld)\n",
				PTR_ERR(data->regmap));
		ret = PTR_ERR(data->regmap);
		goto err_reset_gpio_out;
	}

	data->lt8912_client = client;

	ret = lt8912_init_input(data);
	if (ret) {
		dev_err(&client->dev, "input init failed\n");
		goto err_init_input;
	}

	ret = lt8912_register_dba(data);
	if(ret)
	{
		dev_err(&client->dev, "register msm dba failed\n");
		goto err_regist_dba;
	}

	ret = msm_dba_helper_sysfs_init(&client->dev);
	if(ret)
	{
		dev_err(&client->dev, "add sysfs dba helper failed\n");
		goto err_sys_dba_helper;
	}

	ret = sysfs_create_group(&client->dev.kobj, &lt8912_attr_group);
	if (ret) {
		dev_err(&client->dev, "sysfs create group failed\n");
		goto err_sys_lt8912;
	}

	data->last_status = -1;
	data->lt8912_client = client;
	INIT_DELAYED_WORK(&data->hotplug_work, lt8912_input_work_fn);
	dev_set_drvdata(&client->dev, data);

	return 0;
err_sys_lt8912:
	msm_dba_helper_sysfs_remove(&client->dev);
err_sys_dba_helper:
	msm_dba_remove_probed_device(&data->dba_dev_info);
err_regist_dba:
	if(data->input_hotplug)
		input_free_device(data->input_hotplug);
err_init_input:
err_reset_gpio_out:
	if (gpio_is_valid(data->reset_gpio))
		gpio_free(data->reset_gpio);
err_req_rest_gpio:
	if(data->vreg_1v8)
	{
		regulator_put(data->vreg_1v8);
		data->vreg_1v8 = NULL;
	}

	if(data->vreg_3v3)
	{
		regulator_put(data->vreg_3v3);
		data->vreg_3v3 = NULL;
	}

	if(data->vreg_5v)
	{
		regulator_put(data->vreg_5v);
		data->vreg_5v = NULL;
	}
out:
	return ret;
}

static int lontium_i2c_remove(struct i2c_client *client)
{
	struct lt8912_data *data = dev_get_drvdata(&client->dev);
	if(!data)
		return 0;

	cancel_delayed_work_sync(&data->hotplug_work);
	msm_dba_helper_sysfs_remove(&client->dev);
	msm_dba_remove_probed_device(&data->dba_dev_info);
	sysfs_remove_group(&client->dev.kobj, &lt8912_attr_group);

	if (gpio_is_valid(data->reset_gpio))
		gpio_free(data->reset_gpio);

	if(data->vreg_1v8)
		regulator_put(data->vreg_1v8);
	if(data->vreg_3v3)
		regulator_put(data->vreg_3v3);
	if(data->vreg_5v)
		regulator_put(data->vreg_5v);

	return 0;
}

static int lontium_i2c_suspend(struct device *dev)
{
	int ret;
	struct lt8912_data *data = dev_get_drvdata(dev);
	cancel_delayed_work_sync(&data->hotplug_work);
	ret = lt8912_set_power(data, 0);
	if(ret)
		pr_err("%s: set power off failed\n", __func__);
	return ret;

	return 0;
}

static int lontium_i2c_resume(struct device *dev)
{
	int ret;
	struct lt8912_data *data = dev_get_drvdata(dev);
	ret = lt8912_set_power(data, 1);
	if(ret)
		pr_err("%s: set power on failed\n", __func__);
	return ret;
	return 0;
}

static const struct dev_pm_ops lontium_i2c_pm_ops = {
	.suspend = lontium_i2c_suspend,
	.resume = lontium_i2c_resume,
};

static struct i2c_driver lontium_i2c_driver = {

	.driver = {
		.name = "lontium_i2c",
		.owner = THIS_MODULE,
		.of_match_table = of_rk_lt8912_match,
		.pm = &lontium_i2c_pm_ops,
	},
	.probe = lontium_i2c_probe,
	.remove = lontium_i2c_remove,
	.id_table = lt8912_id,
};

static const struct of_device_id of_hdmi_edid_match[] = {
	{ .compatible = "hdmi,edid"},
	{  },
};

static int hdmi_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "hdmid interface i2c check failed.\n");
		return -ENODEV;
	}
	hdmi_i2c_client = client;
	lt8912_debug("enter %s\n", __func__);
	return 0;
}

static int hdmi_i2c_remove(struct i2c_client *client)
{
	hdmi_i2c_client = NULL;
	return 0;
}

static const struct i2c_device_id hdmi_edid_id[] = {
	{"hdmi,edid", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, lt8912_id);

static struct i2c_driver hdmi_i2c_driver = {
	.driver = {
		.name = "hdmi,edid",
		.owner = THIS_MODULE,
		.of_match_table = of_hdmi_edid_match,
	},
	.probe = hdmi_i2c_probe,
	.remove = hdmi_i2c_remove,
	.id_table = hdmi_edid_id,
};

static int __init lontium_dba_init(void)
{
	int ret;
	ret = i2c_add_driver(&hdmi_i2c_driver);
	if(ret)
	{
		pr_err("%s: add hdmi i2c driver erro-%d\n", __func__, ret);
		return ret;
	}

	ret = i2c_add_driver(&lontium_i2c_driver);

	return ret;
}

static void __exit lontium_dba_exit(void)
{
	i2c_del_driver(&lontium_i2c_driver);
	i2c_del_driver(&hdmi_i2c_driver);
}

module_init(lontium_dba_init);
module_exit(lontium_dba_exit);

MODULE_DESCRIPTION("LT8912_DBA_DRIVER");
