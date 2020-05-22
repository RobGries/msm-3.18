/*
 * 
 */
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include "cg5251_poll.h"

#define DRIVER_VERSION "1.0"
#define DRIVER_NAME "cg_als_i2c"
#define DEVICE_NAME "cg5251"

#define ALS_NAME "lightsensor-level"
#define ALS_LUX_MAX 1000
#define ALS_LUX_THD 20
#define ALS_DLY_MIN 400
#define ALS_SCL     100000

/*#define AGAIN_REG 0x05
#define EGAIN_REG 0x08
#define ADATAL_REG 0x21
#define ADATAH_REG 0x22*/

struct device_data *g_psdevicedata = NULL;
static struct platform_device *g_psplatformdevice = NULL;

static int32_t get_als_lux(struct device_data *psdevicedata)
{
    uint8_t aui8data[2] = {0};
    uint8_t i = 0;
    uint8_t j = 33;
    int32_t si32ADATA = 0;
    int32_t result;

    for(; i < ARRAY_SIZE(aui8data); i ++, j ++) 
    {
        aui8data[i] = i2c_smbus_read_byte_data(psdevicedata->psclient, j);
    }

    si32ADATA = MAKEWORD(aui8data[0], aui8data[1]);

    result = ((si32ADATA * 148 * psdevicedata->sals.ui32devparam) / psdevicedata->sals.sreg.ui8TIG_SEL / ALS_SCL);

    g_psdevicedata->sals.ui32lux = result;

    return result;
}

inline void report_event(struct input_dev *dev,
                                int32_t   report_value)
{
    input_report_abs(dev,
                     ABS_MISC,
                     report_value);


    input_sync(dev);

    return;
}

static int als_polling(void *parg)
{
    struct device_data *psdevicedata = parg;

    uint32_t lux = 0;
    uint32_t delay = 0;

    if(NULL == psdevicedata) {
        return -EINVAL;
    }

    init_completion(&psdevicedata->scompletion);

    while(1) {
        mutex_lock(&psdevicedata->smutexsync);

        delay = psdevicedata->sals.ui32delay;

        lux = get_als_lux(psdevicedata);

        report_event(psdevicedata->sinput_dev, lux);

        if(0 == psdevicedata->ui8ThreadRunning) {
            break;
        }

        mutex_unlock(&psdevicedata->smutexsync);

        msleep(delay);
    };

    mutex_unlock(&psdevicedata->smutexsync);

    complete(&psdevicedata->scompletion);

    return 0;
}

static int32_t enable(struct device_data *psdevicedata,
                             uint32_t    enable)
{
    if(NULL == psdevicedata) {
        return -EINVAL;
    }

    if(0 != enable) {
        if(0 == psdevicedata->ui8ThreadRunning) {
            psdevicedata->sals.ui32lux = 0;
            psdevicedata->ui8ThreadRunning = 1;

            psdevicedata->psworker = kthread_run(als_polling,
                                                 psdevicedata,
                                                 "als_polling");
        } else {
            printk(KERN_WARNING "%s: " "thread is already running\n",
                   DEVICE_NAME);
        }
    } else {
        if(0 != psdevicedata->ui8ThreadRunning) {
            psdevicedata->ui8ThreadRunning = 0;

            mutex_unlock(&psdevicedata->smutexsync);

            wait_for_completion(&psdevicedata->scompletion);

            mutex_lock(&psdevicedata->smutexsync);

            psdevicedata->psworker = NULL;
        }
    }

    return 0;
}

static int32_t init_all_setting(struct device_data *psdevicedata)
{
    uint8_t aui8data[5] = {0};
    uint8_t i = 0;
    uint8_t j = 4;

    for(; i < ARRAY_SIZE(aui8data); i ++, j ++) {
        aui8data[i] = i2c_smbus_read_byte_data(psdevicedata->psclient,
                                               j);
    }

    psdevicedata->sals.sreg.ui8TIG_SEL = aui8data[0];
    psdevicedata->sals.sreg.ui8AGAIN = (aui8data[1] & 0x03);
    psdevicedata->sals.sreg.ui8EGAIN = (aui8data[4] & 0x01);

    if(0 == psdevicedata->sals.sreg.ui8AGAIN) {
        if(0 == psdevicedata->sals.sreg.ui8EGAIN) {
            psdevicedata->sals.ui32devparam = 512000;
        }
        else {
            psdevicedata->sals.ui32devparam = 32000;
        }
    }
    else if(1 == psdevicedata->sals.sreg.ui8AGAIN) {
        if(0 == psdevicedata->sals.sreg.ui8EGAIN) {
            
        }
        else {
            psdevicedata->sals.ui32devparam = 8000;
        }
    }
    else if(2 == psdevicedata->sals.sreg.ui8AGAIN) {
        if(0 == psdevicedata->sals.sreg.ui8EGAIN) {
            
        }
        else {
            psdevicedata->sals.ui32devparam = 2000;
        }
    }
    else {
        if(0 == psdevicedata->sals.sreg.ui8EGAIN) {
            
        }
        else {
            psdevicedata->sals.ui32devparam = 526;
        }
    }

    enable(psdevicedata,
           0);

    return 0;
}

static ssize_t als_lux_range_show(struct kobject        *kobj,
                                  struct kobj_attribute *attr,
                                         char           *buf)
{
    return sprintf(buf, "%d\n", ALS_LUX_MAX);
}

static ssize_t als_lux_show(struct kobject        *kobj,
                            struct kobj_attribute *attr,
                                   char           *buf)
{
    int32_t als_lux = 0;

    mutex_lock(&g_psdevicedata->smutexsync);

    als_lux = g_psdevicedata->sals.ui32lux;

    mutex_unlock(&g_psdevicedata->smutexsync);

    return sprintf(buf, "%d lux\n", als_lux);
}

static ssize_t als_lux_store(       struct kobject        *kobj,
                                    struct kobj_attribute *attr,
                             const         char           *buf,
                                           size_t         len)
{
    unsigned long value = simple_strtoul(buf, NULL, 10);

    mutex_lock(&g_psdevicedata->smutexsync);

    report_event(g_psdevicedata->sinput_dev,
                 value);

    mutex_unlock(&g_psdevicedata->smutexsync);

    return len;
}

static ssize_t als_enable_show(struct kobject        *kobj,
                               struct kobj_attribute *attr,
                                      char           *buf)
{
    int32_t enable = 0;

    mutex_lock(&g_psdevicedata->smutexsync);

    enable = g_psdevicedata->ui8ThreadRunning;

    mutex_unlock(&g_psdevicedata->smutexsync);

    return sprintf(buf, "%d\n", enable);
}

static ssize_t als_enable_store(      struct kobject        *kobj,
                                      struct kobj_attribute *attr,
                                const        char           *buf,
                                             size_t         len)
{
    uint32_t value = simple_strtoul(buf, NULL, 10);

    printk(KERN_INFO "%s: " "Enable ALS : %d\n",
           DEVICE_NAME,
           value);

    mutex_lock(&g_psdevicedata->smutexsync);

    enable(g_psdevicedata,
           value);

    mutex_unlock(&g_psdevicedata->smutexsync);

    return len;
}

static ssize_t als_lux_res_show(struct kobject        *kobj,
                                struct kobj_attribute *attr,
                                       char           *buf)
{
    return sprintf(buf, "1\n");
}

CG_KOBJ_ATTR_RO(als_lux_range);
CG_KOBJ_ATTR_RW(als_lux);
CG_KOBJ_ATTR_RW(als_enable);
CG_KOBJ_ATTR_RO(als_lux_res);

static struct attribute *g_psattributes[] =
{
    &als_lux_range_kobj_attribute.attr,
    &als_lux_kobj_attribute.attr,
    &als_enable_kobj_attribute.attr,
    &als_lux_res_kobj_attribute.attr,
    NULL,
};

static int i2c_probe(      struct i2c_client    *psclient,
                     const struct i2c_device_id *psid)
{
    struct device_data *psdevicedata = NULL;

    int ret = 0;

    printk(KERN_INFO "%s: " "slvadr = 0x%x\n",
           DEVICE_NAME,
           psclient->addr);

    ret = i2c_check_functionality(psclient->adapter,
                                  I2C_FUNC_SMBUS_BYTE_DATA);

    if(1 != ret) {
        printk(KERN_ERR "%s: " "i2c_check_functionality() == %d\n",
               DEVICE_NAME,
               ret);

        return -ENODEV;
    }

    psdevicedata = kzalloc(sizeof(struct device_data),
                           GFP_KERNEL);

    if(unlikely(NULL == psdevicedata)) {
        printk(KERN_ERR "%s: " "kzalloc() = 0x%p\n",
               DEVICE_NAME,
               psdevicedata);

        return -ENOMEM;
    }

    i2c_set_clientdata(psclient,
                       psdevicedata);

    g_psdevicedata = psdevicedata;

    psdevicedata->psclient = psclient;

    psdevicedata->sinput_dev = input_allocate_device();

    if(NULL == psdevicedata->sinput_dev) {
        ret = -ENOMEM;

        goto CLEANUP;
    }

    mutex_init(&psdevicedata->smutexsync);

    psdevicedata->psworker = NULL;
    psdevicedata->scompletion;
    psdevicedata->ui8ThreadRunning = 0;
    psdevicedata->sals.sreg.ui8TIG_SEL = 0;
    psdevicedata->sals.sreg.ui8AGAIN = 0;
    psdevicedata->sals.sreg.ui8EGAIN = 0;
    psdevicedata->sals.ui32devparam = 0;
    psdevicedata->sals.ui32delay = ALS_DLY_MIN;
    psdevicedata->sals.ui32lux = 0;

    psdevicedata->sinput_dev->name = ALS_NAME;

    set_bit(EV_ABS,
            psdevicedata->sinput_dev->evbit);

    input_set_abs_params(psdevicedata->sinput_dev,
                         ABS_MISC,
                         0,
                         ALS_LUX_MAX,
                         0,
                         0);

    ret = input_register_device(psdevicedata->sinput_dev);

    if(0 > ret) {
        printk(KERN_ERR "%s: " "input_register_device() = %d\n",
               DEVICE_NAME,
               ret);

        goto CLEANUP;
    }

    init_all_setting(psdevicedata);

    return 0;
CLEANUP:
    if(NULL != psdevicedata) {
        mutex_destroy(&psdevicedata->smutexsync);

        input_free_device(psdevicedata->sinput_dev);

        kfree(psdevicedata);

        psdevicedata = NULL;
    }

    return ret;
}

static int i2c_remove(struct i2c_client *psclient)
{
    struct device_data *psdevicedata = i2c_get_clientdata(psclient);

    int i = 0;

    platform_device_put(g_psplatformdevice);

    for(i = 0; NULL != g_psattributes[i]; i ++) {
        sysfs_remove_file(&g_psplatformdevice->dev.kobj,
                          g_psattributes[i]);
    }

    if(NULL != psdevicedata) {
        mutex_destroy(&psdevicedata->smutexsync);

        input_unregister_device(psdevicedata->sinput_dev);

        input_free_device(psdevicedata->sinput_dev);

        kfree(psdevicedata);

        psdevicedata = NULL;
    }

    return 0;
}

static const struct i2c_device_id g_si2cdeviceid[] = {
    {"cg5251", 0},
    { }
};

MODULE_DEVICE_TABLE(i2c, g_si2cdeviceid);
static const struct of_device_id cg5251_of_match[] = {
    { .compatible = "cg,cg5251" },
    { }
};

static struct i2c_driver g_si2cdriver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = of_match_ptr(cg5251_of_match),
    },
    .probe = i2c_probe,
    .remove = i2c_remove,
    .id_table = g_si2cdeviceid,
};

static int __init i2c_init(void)
{
    int ret = 0;
    int i = 0;

    ret = i2c_add_driver(&g_si2cdriver);

    if(0 != ret) {
        printk(KERN_ERR "%s: " "i2c_add_driver() = %d\n",
               DEVICE_NAME,
               ret);

        return ret;
    }

    g_psplatformdevice = platform_device_alloc(DEVICE_NAME,
                                               -1);

    if(NULL == g_psplatformdevice) {
        goto CLEANUP;
    }

    ret = platform_device_add(g_psplatformdevice);

    if(0 != ret) {
       goto CLEANUP;
    }

    for(i = 0; NULL != g_psattributes[i]; i ++) {
        ret = sysfs_create_file(&g_psplatformdevice->dev.kobj,
                                g_psattributes[i]);

        if(0 != ret) {
            goto CLEANUP;
        }
    }

    return 0;
CLEANUP:
    i2c_del_driver(&g_si2cdriver);

    return -ENOMEM;
}

static void __exit i2c_exit(void)
{
    i2c_del_driver(&g_si2cdriver);

    return;
}

MODULE_AUTHOR("andy<@chipgoal.com>");
MODULE_DESCRIPTION("Chipgoal Ambient Light Sensor driver");
MODULE_LICENSE("GPL");

module_init(i2c_init);
module_exit(i2c_exit);
