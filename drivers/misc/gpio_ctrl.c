#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

struct gpio_device {
	unsigned int gpio;
	unsigned int value;
	const char *name;
	struct kobject *kobj;
};

struct gpio_device *gpio_ctrl;

int node_num = 0;

static struct gpio_device *find_gpio_device(const char *label)
{
	int i = 0;
	for(i = 0; i < node_num; i++) {
		if (!strcmp(gpio_ctrl[i].name, label)) {
			return &gpio_ctrl[i];
		}
	}
	return NULL;
}

static ssize_t get_gpio_value(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct gpio_device *gpio_dev = find_gpio_device(kobj->name);
	return sprintf(buf, "%d\n", gpio_get_value(gpio_dev->gpio));
}

static ssize_t set_gpio_value(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t size)
{
	unsigned long val;
	struct gpio_device *gpio_dev = find_gpio_device(kobj->name);
	if (kstrtoul(buf, 10, &val))
		return -EINVAL;
	gpio_dev->value = val;
	gpio_direction_output(gpio_dev->gpio, gpio_dev->value);
	return size;
}
static struct kobj_attribute gpio_value_attr =
__ATTR(value, 0664, get_gpio_value, set_gpio_value);

static struct attribute *gpio_ctrl_attributes[] = {
	&gpio_value_attr.attr,
	NULL
};

static struct attribute_group gpio_ctrl_attribute_group = {
	.attrs = gpio_ctrl_attributes
};

static int gpio_ctrl_sysfs_init(struct gpio_device *gpio_dev)
{
	int err;
	gpio_dev->kobj = kobject_create_and_add(gpio_dev->name, kernel_kobj);
	if (!gpio_dev->kobj) {
		return -ENOMEM;
	}
	err = sysfs_create_group(gpio_dev->kobj, &gpio_ctrl_attribute_group);
	if (err) {
		pr_err("gpio ctrl sysfs_create_group fail\n");
		return err;
	}
	return 0;
}

static int create_gpio_interface(struct gpio_device *gpio_dev)
{
	int ret, state = 0;
	if (!gpio_is_valid(gpio_dev->gpio)) {
		pr_info("Skipping unavailable gpio %d (%s)\n",
				gpio_dev->gpio, gpio_dev->name);
		return 0;
	}

	ret = gpio_request(gpio_dev->gpio, gpio_dev->name);
	if (ret < 0)
		return ret;
	ret = gpio_direction_output(gpio_dev->gpio, state);
	if (ret < 0)
		goto err;
	gpio_ctrl_sysfs_init(gpio_dev);
	return 0;
err:
	gpio_free(gpio_dev->gpio);
	return ret;
}

static int gpio_ctrl_probe(struct platform_device *pdev)
{
	int ret;
	struct gpio_device *gpio_dev;
	struct device_node *node, *child;
	enum of_gpio_flags flags;
	int child_i = 0;
	node = pdev->dev.of_node;
	if (node == NULL)
		return -ENODEV;
	child = NULL;
	pr_info("qpio_ctrl_probe successful\n");
	while ((child = of_get_next_child(node, child))) {
		node_num++;
	}

	gpio_ctrl = devm_kzalloc(&pdev->dev,
			(sizeof(struct gpio_device) * node_num), GFP_KERNEL);
	if(!gpio_ctrl) {
		pr_err("gpio_ctrl alloc memeory failed!\n");
		return -ENOMEM;
	}
	for_each_child_of_node(node, child) {
		gpio_dev = &gpio_ctrl[child_i];
		gpio_dev->gpio = of_get_gpio_flags(child, 0, &flags);
		if (!gpio_is_valid(gpio_dev->gpio)) {
			pr_err("invalid gpio #%d\n", gpio_dev->gpio);
			return -ENODEV;
		}
		gpio_dev->name = of_get_property(child, "label", NULL) ? : child->name;

		ret = create_gpio_interface(gpio_dev);
		if (ret < 0) {
			pr_err("create gpio interface fail ret = %d\n", ret);
			return -ENODEV;
		}
		child_i++;
	}

	return 0;
}

static int gpio_ctrl_remove(struct platform_device *pdev)
{
	pr_info("gpio ctrl remove\n");
	if (!gpio_is_valid(gpio_ctrl->gpio))
		return -ENODEV;
	gpio_free(gpio_ctrl->gpio);
	kfree(gpio_ctrl);
	return 0;
}

static struct of_device_id  gpio_ctrl_match_table[] = {
	{ .compatible = "gpio_ctrl",},
	{},
};

static struct platform_driver gpio_ctrl_driver = {
	.driver = {
		.name = "gpio_ctrl",
		.owner = THIS_MODULE,
		.of_match_table = gpio_ctrl_match_table,
	},
	.probe = gpio_ctrl_probe,
	.remove = gpio_ctrl_remove,
};

static int gpio_ctrl_init(void)
{
	return platform_driver_register(&gpio_ctrl_driver);
}

static void gpio_ctrl_exit(void)
{
	platform_driver_unregister(&gpio_ctrl_driver);
}

module_init(gpio_ctrl_init);
module_exit(gpio_ctrl_exit);

MODULE_DESCRIPTION("gpio output control driver");
MODULE_LICENSE("GPL");
