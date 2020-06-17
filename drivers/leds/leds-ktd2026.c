/* ktd2026 Driver V0.1 2020.6.18 by Lu Kunpeng Thundercomm Co. */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

struct ktd2026_data {
	struct i2c_client *i2c_client;
	struct regulator *vcc_i2c;
	struct kobject *leds_kobj;
	struct mutex i2c_rw_access;
} data;

#ifdef KTD2026_READ
int ktd2026_i2c_read(struct i2c_client *client, char *writebuf,
		int writelen, char *readbuf, int readlen)
{
	int ret = -EIO;

	mutex_lock(&data.i2c_rw_access);

	if (readlen > 0) {
		if (writelen > 0) {
			struct i2c_msg msgs[] = {
				{
					.addr = client->addr,
					.flags = 0,
					.len = writelen,
					.buf = writebuf,
				},
				{
					.addr = client->addr,
					.flags = I2C_M_RD,
					.len = readlen,
					.buf = readbuf,
				},
			};
			ret = i2c_transfer(client->adapter, msgs, 2);
			if (ret < 0)
				pr_err("%s: i2c_read error, ret=%d", __func__,
						ret);
		} else {
			struct i2c_msg msgs[] = {
				{
					.addr = client->addr,
					.flags = I2C_M_RD,
					.len = readlen,
					.buf = readbuf,
				},
			};
			ret = i2c_transfer(client->adapter, msgs, 1);
			if (ret < 0)
				pr_err("%s: i2c_read error, ret=%d", __func__,
						ret);
		}
	}

	mutex_unlock(&data.i2c_rw_access);

	return ret;
}
#endif

int ktd2026_i2c_write(struct i2c_client *client, char *writebuf, int writelen)
{
	int ret = 0;

	mutex_lock(&data.i2c_rw_access);

	if (writelen > 0) {
		struct i2c_msg msgs[] = {
			{
				.addr = client->addr,
				.flags = 0,
				.len = writelen,
				.buf = writebuf,
			},
		};
		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0)
			pr_err("%s: i2c_write error, ret=%d", __func__, ret);
	}

	mutex_unlock(&data.i2c_rw_access);

	return ret;
}

int ktd2026_write_reg(struct i2c_client *client, u8 regaddr, u8 regvalue)
{
	u8 buf[2] = {0};

	buf[0] = regaddr;
	buf[1] = regvalue;
	return ktd2026_i2c_write(client, buf, sizeof(buf));
}

#ifdef KTD2026_READ
int ktd2026_read_reg(struct i2c_client *client, u8 regaddr, u8 *regvalue)
{
	return ktd2026_i2c_read(client, &regaddr, 1, regvalue, 1);
}
#endif

static ssize_t ktd2026_mode_on_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	u8 val = 0;

	if (!strcmp("rgb\n", buf))
		val = 0x15;
	else {
#ifdef KTD2026_READ
		ret = ktd2026_read_reg(data.i2c_client, 0x04, &val);
#endif
		if (!strcmp("r\n", buf))
			val = (val & 0xFC) | 0x01;
		else if (!strcmp("g\n", buf))
			val = (val & 0xF3) | 0x04;
		else if (!strcmp("b\n", buf))
			val = (val & 0xCF) | 0x10;
		else if (!strcmp("rg\n", buf))
			val = (val & 0xF0) | 0x05;
		else if (!strcmp("rb\n", buf))
			val = (val & 0xCC) | 0x11;
		else if (!strcmp("gb\n", buf))
			val = (val & 0xC3) | 0x14;
		else {
			pr_err("%s: wrong val\n", __func__);
			return len;
		}
	}
	ret = ktd2026_write_reg(data.i2c_client, 0x04, val);

	return len;
}

static ssize_t ktd2026_mode_off_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	u8 val = 0;

	if (!strcmp("rgb\n", buf))
		val = 0x00;
	else {
#ifdef KTD2026_READ
		ret = ktd2026_read_reg(data.i2c_client, 0x04, &val);
#endif
		if (!strcmp("r\n", buf))
			val = val & 0xFC;
		else if (!strcmp("g\n", buf))
			val = val & 0xF3;
		else if (!strcmp("b\n", buf))
			val = val & 0xCF;
		else if (!strcmp("rg\n", buf))
			val = val & 0xF0;
		else if (!strcmp("rb\n", buf))
			val = val & 0xCC;
		else if (!strcmp("gb\n", buf))
			val = val & 0xC3;
		else {
			pr_err("%s: wrong val\n", __func__);
			return len;
		}
	}
	ret = ktd2026_write_reg(data.i2c_client, 0x04, val);

	return len;
}

static ssize_t ktd2026_mode_pwm1_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	u8 val = 0;

	if (!strcmp("rgb\n", buf))
		val = 0xaa;
	else {
#ifdef KTD2026_READ
		ret = ktd2026_read_reg(data.i2c_client, 0x04, &val);
#endif
		if (!strcmp("r\n", buf))
			val = (val & 0xFC) | 0x02;
		else if (!strcmp("g\n", buf))
			val = (val & 0xF3) | 0x08;
		else if (!strcmp("b\n", buf))
			val = (val & 0xCF) | 0x20;
		else if (!strcmp("rg\n", buf))
			val = (val & 0xF0) | 0x0a;
		else if (!strcmp("rb\n", buf))
			val = (val & 0xCC) | 0x22;
		else if (!strcmp("gb\n", buf))
			val = (val & 0xC3) | 0x28;
		else {
			pr_err("%s: wrong val\n", __func__);
			return len;
		}
	}
	ret = ktd2026_write_reg(data.i2c_client, 0x04, val);

	return len;
}

static ssize_t ktd2026_mode_pwm2_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	u8 val = 0;

	if (!strcmp("rgb\n", buf))
		val = 0xff;
	else {
#ifdef KTD2026_READ
		ret = ktd2026_read_reg(data.i2c_client, 0x04, &val);
#endif
		if (!strcmp("r\n", buf))
			val = (val & 0xFC) | 0x03;
		else if (!strcmp("g\n", buf))
			val = (val & 0xF3) | 0x0c;
		else if (!strcmp("b\n", buf))
			val = (val & 0xCF) | 0x30;
		else if (!strcmp("rg\n", buf))
			val = (val & 0xF0) | 0x0f;
		else if (!strcmp("rb\n", buf))
			val = (val & 0xCC) | 0x33;
		else if (!strcmp("gb\n", buf))
			val = (val & 0xC3) | 0x3c;
		else {
			pr_err("%s: wrong val\n", __func__);
			return len;
		}
	}
	ret = ktd2026_write_reg(data.i2c_client, 0x04, val);

	return len;
}

static ssize_t ktd2026_period_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(buf, 10, &val);
	if (val > 0x7F) {
		pr_err("%s: val %lu out of range (0, 127)\n", __func__, val);
		return len;
	} // period = (1.28s ~ 1.3s) * val
	ret = ktd2026_write_reg(data.i2c_client, 0x01, (u8)val);

	return len;
}

static ssize_t ktd2026_timer1_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(buf, 10, &val);
	if (val > 0xFF) {
		pr_err("%s: val %lu out of range (0, 255)\n", __func__, val);
		return len;
	} // duty percent = 0.4% * val
	ret = ktd2026_write_reg(data.i2c_client, 0x02, (u8)val);

	return len;
}

static ssize_t ktd2026_timer2_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(buf, 10, &val);
	if (val > 0xFF) {
		pr_err("%s: val %lu out of range (0, 255)\n", __func__, val);
		return len;
	} // duty of percent = 0.4% * val
	ret = ktd2026_write_reg(data.i2c_client, 0x03, (u8)val);

	return len;
}

static ssize_t ktd2026_tslot_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long tslot = 0;
	u8 val = 0;

	ret = kstrtoul(buf, 10, &tslot);
	if (tslot > 3) {
		pr_err("%s: val %lu out of range (0, 3)\n", __func__, tslot);
		return len;
	}

#ifdef KTD2026_READ
	ret = ktd2026_read_reg(data.i2c_client, 0x00, &val);
#endif
	val = (val & 0xFC) | ((u8)tslot & 0x03);
	ret = ktd2026_write_reg(data.i2c_client, 0x00, val);

	return len;
}

static ssize_t ktd2026_tscale_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	u8 val = 0;

#ifdef KTD2026_READ
	ret = ktd2026_read_reg(data.i2c_client, 0x04, &val);
#endif
	if (!strcmp("n\n", buf))
		val = val & 0x9F;
	else if (!strcmp("s\n", buf))
		val = (val & 0x9F) | 0x20;
	else if (!strcmp("ss\n", buf))
		val = (val & 0x9F) | 0x40;
	else if (!strcmp("fff\n", buf))
		val = (val & 0x9F) | 0x60;
	else {
		pr_err("%s: wrong val\n", __func__);
		return len;
	}
	ret = ktd2026_write_reg(data.i2c_client, 0x00, val);

	return len;
}

static ssize_t ktd2026_trise_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long trise = 0;
	u8 val = 0;

	ret = kstrtoul(buf, 10, &trise);
	if (trise > 0xF) {
		pr_err("%s: val %lu out of range (0, 15)\n", __func__, trise);
		return len;
	} // trise = 96ms * tscale * val

#ifdef KTD2026_READ
	ret = ktd2026_read_reg(data.i2c_client, 0x05, &val);
#endif
	val = (val & 0xF0) | ((u8)trise & 0x0F);
	ret = ktd2026_write_reg(data.i2c_client, 0x05, val);

	return len;
}

static ssize_t ktd2026_tfall_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long tfall = 0;
	u8 val = 0;

	ret = kstrtoul(buf, 10, &tfall);
	if (tfall > 0xF) {
		pr_err("%s: val %lu out of range (0, 15)\n", __func__, tfall);
		return len;
	} // tfall = 96ms * tscale * val

#ifdef KTD2026_READ
	ret = ktd2026_read_reg(data.i2c_client, 0x05, &val);
#endif
	val = (val & 0x0F) | ((u8)tfall << 4);
	ret = ktd2026_write_reg(data.i2c_client, 0x05, val);

	return len;
}

static ssize_t ktd2026_trisefall_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(buf, 10, &val);
	if (val > 0xFF) {
		pr_err("%s: val %lu out of range (0, 255)\n", __func__, val);
		return len;
	} // trise or tfall = 96ms * tscale * val

	ret = ktd2026_write_reg(data.i2c_client, 0x05, (u8)val);

	return len;
}

static ssize_t ktd2026_red_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(buf, 10, &val);
	if (val > 0xFF) {
		pr_err("%s: val %lu out of range (0, 255)\n", __func__, val);
		return len;
	}
	ret = ktd2026_write_reg(data.i2c_client, 0x06, (u8)val);

	return len;
}

static ssize_t ktd2026_green_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(buf, 10, &val);
	if (val > 0xFF) {
		pr_err("%s: val %lu out of range (0, 255)\n", __func__, val);
		return len;
	}
	ret = ktd2026_write_reg(data.i2c_client, 0x07, (u8)val);

	return len;
}

static ssize_t ktd2026_blue_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(buf, 10, &val);
	if (val > 0xFF) {
		pr_err("%s: val %lu out of range (0, 255)\n", __func__, val);
		return len;
	}
	ret = ktd2026_write_reg(data.i2c_client, 0x08, (u8)val);

	return len;
}

static u8 addr;
static ssize_t ktd2026_addr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(buf, 10, &val);
	if (val > 8) {
		pr_err("%s: addr %lu out of range (0, 8)\n", __func__, val);
		return len;
	}
	addr = (u8)val;

	return len;
}

static ssize_t ktd2026_addr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", addr);
}

static ssize_t ktd2026_data_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(buf, 10, &val);
	if (val > 0xFF) {
		pr_err("%s: val %lu out of range (0, 255)\n", __func__, val);
		return len;
	}
	ret = ktd2026_write_reg(data.i2c_client, addr, (u8)val);

	pr_info("%s: write reg %u, val %lu, buf %s", __func__, addr, val, buf);

	return len;
}

#ifdef KTD2026_READ
static ssize_t ktd2026_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u8 val = 0;
	ktd2026_read_reg(data.i2c_client, addr, &val);
	return snprintf(buf, PAGE_SIZE, "%u %u\n", addr, val);
}
#endif

static DEVICE_ATTR(mode_on, 0664, NULL, ktd2026_mode_on_store);
static DEVICE_ATTR(mode_off, 0664, NULL, ktd2026_mode_off_store);
static DEVICE_ATTR(mode_pwm1, 0664, NULL, ktd2026_mode_pwm1_store);
static DEVICE_ATTR(mode_pwm2, 0664, NULL, ktd2026_mode_pwm2_store);
static DEVICE_ATTR(period, 0664, NULL, ktd2026_period_store);
static DEVICE_ATTR(timer1, 0664, NULL, ktd2026_timer1_store);
static DEVICE_ATTR(timer2, 0664, NULL, ktd2026_timer2_store);
static DEVICE_ATTR(tslot, 0664, NULL, ktd2026_tslot_store);
static DEVICE_ATTR(tscale, 0664, NULL, ktd2026_tscale_store);
static DEVICE_ATTR(trise, 0664, NULL, ktd2026_trise_store);
static DEVICE_ATTR(tfall, 0664, NULL, ktd2026_tfall_store);
static DEVICE_ATTR(trisefall, 0664, NULL, ktd2026_trisefall_store);
static DEVICE_ATTR(red_brightness, 0664, NULL, ktd2026_red_brightness_store);
static DEVICE_ATTR(green_brightness, 0664, NULL,ktd2026_green_brightness_store);
static DEVICE_ATTR(blue_brightness, 0664, NULL, ktd2026_blue_brightness_store);
static DEVICE_ATTR(addr, 0664, ktd2026_addr_show, ktd2026_addr_store);
#ifdef KTD2026_READ
static DEVICE_ATTR(data, 0664, ktd2026_data_show, ktd2026_data_store);
#else
static DEVICE_ATTR(data, 0664, NULL, ktd2026_data_store);
#endif

static struct attribute *leds_attrs[] = {
	&dev_attr_mode_on.attr,
	&dev_attr_mode_off.attr,
	&dev_attr_mode_pwm1.attr,
	&dev_attr_mode_pwm2.attr,
	&dev_attr_period.attr,
	&dev_attr_timer1.attr,
	&dev_attr_timer2.attr,
	&dev_attr_tslot.attr,
	&dev_attr_tscale.attr,
	&dev_attr_trise.attr,
	&dev_attr_tfall.attr,
	&dev_attr_trisefall.attr,
	&dev_attr_red_brightness.attr,
	&dev_attr_green_brightness.attr,
	&dev_attr_blue_brightness.attr,
	&dev_attr_addr.attr,
	&dev_attr_data.attr,
	NULL,
};

static struct attribute_group leds_attr_group = {
	.attrs = leds_attrs,
};

static int ktd2026_i2c_power_on(struct i2c_client *client)
{
	int ret = 0;

	data.vcc_i2c = regulator_get(&client->dev, "vcc_i2c");
	if (IS_ERR(data.vcc_i2c)) {
		ret = PTR_ERR(data.vcc_i2c);
		pr_err("%s: ktd2026 get regulator failed\n", __func__);
		return ret;
	}

	ret = regulator_set_voltage(data.vcc_i2c, 1800000, 1800000);
	if (ret) {
		pr_err("%s: ktd2026 set regulator voltage failed\n", __func__);
		goto out;
	}

	ret = regulator_enable(data.vcc_i2c);
	if (ret) {
		pr_err("%s: ktd2026 enable regulator failed\n", __func__);
		goto out;
	}

	return ret;

out:
	regulator_put(data.vcc_i2c);
	return ret;
}

static int ktd2026_init(struct i2c_client *client)
{
	int ret = 0;
	u8 val = 0;

	val = 0x7; // chip reset
	ret = ktd2026_write_reg(client, 0x00, val);
	if (ret)
		pr_info("%s: ktd2026 init 1 failed\n", __func__);

	usleep_range(200, 300);

	val = 0x18; // device always on
	ret = ktd2026_write_reg(client, 0x00, val);
	if (ret)
		pr_info("%s: ktd2026 init 2 failed\n", __func__);

	return ret;
}

static int ktd2026_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	pr_info("%s: ktd2026 probe start\n", __func__);
	mutex_init(&data.i2c_rw_access);
	data.i2c_client = client;

	if (ktd2026_i2c_power_on(client)) {
		pr_info("%s: ktd2026 i2c power on failed\n", __func__);
		goto out_;
	}
	//if (ktd2026_init(client))
	//	goto out;
	ktd2026_init(client);

	data.leds_kobj = kobject_create_and_add("ktd2026_leds", kernel_kobj);
	if (!data.leds_kobj) {
		pr_err("%s: kobj create failed\n", __func__);
		goto out;
	}

	if (sysfs_create_group(data.leds_kobj, &leds_attr_group)) {
		pr_err("%s: sysfs create failed\n", __func__);
		goto out;
	}

	pr_info("%s: ktd2026 probe end\n", __func__);
	return 0;

out:
	regulator_put(data.vcc_i2c);
out_:
	pr_err("%s: ktd2026 probe failed\n", __func__);
	return -1;
}

static int ktd2026_remove(struct i2c_client *client)
{
	sysfs_remove_group(data.leds_kobj, &leds_attr_group);
	regulator_put(data.vcc_i2c);

	return 0;
}

#ifdef KTD2026_SUSPEND
static int __maybe_unused ktd2026_suspend(struct device *dev)
{
	int ret;
	//struct i2c_client *client = to_i2c_client(dev);

	return 0;
}

static int __maybe_unused ktd2026_resume(struct device *dev)
{
	int ret;
	//struct i2c_client *client = to_i2c_client(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(ktd2026_pm, ktd2026_suspend, ktd2026_resume);
#else
static SIMPLE_DEV_PM_OPS(ktd2026_pm, NULL, NULL);
#endif

static const struct i2c_device_id ktd2026_id[] = {
	{ "ktd2026", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, ktd2026_id);

static const struct of_device_id of_ktd2026_leds_match[] = {
	{ .compatible = "leds,ktd2026", },
	{},
};

static struct i2c_driver ktd2026_driver = {
	.driver = {
		.name   = "ktd2026",
		.pm = &ktd2026_pm,
		.of_match_table = of_match_ptr(of_ktd2026_leds_match),
	},
	.probe      = ktd2026_probe,
	.remove     = ktd2026_remove,
	.id_table   = ktd2026_id,
};

module_i2c_driver(ktd2026_driver);

MODULE_LICENSE("GPL v2");
