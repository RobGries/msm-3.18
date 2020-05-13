/*
 *
 */
#ifndef __CG5251_POLL_H
#define __CG5251_POLL_H

//#define CG_DBG

#define CG_KOBJ_ATTR_RO(_name) static struct kobj_attribute _name##_kobj_attribute = __ATTR_RO(_name)
#ifdef CG_DBG
#define __ATTR_RW(_name) __ATTR(_name, 0666, _name##_show, _name##_store)
#define CG_KOBJ_ATTR_RW(_name) static struct kobj_attribute _name##_kobj_attribute = __ATTR_RW(_name)
#else
#define __ATTR_RW(_name) __ATTR(_name, 0644, _name##_show, _name##_store)
#define CG_KOBJ_ATTR_RW(_name) static struct kobj_attribute _name##_kobj_attribute = __ATTR_RW(_name)
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#define MAKEWORD(hb, lb) ((uint16_t) (((uint8_t) (hb)) | ((uint16_t) ((uint8_t) (lb))) << 8)) 

struct device_data {
    struct i2c_client *psclient;
    struct input_dev *sinput_dev;
    struct mutex smutexsync;
    struct task_struct *psworker;
    struct completion scompletion;
    uint8_t ui8ThreadRunning;

    struct {
        struct {
            uint8_t ui8TIG_SEL;
            uint8_t ui8AGAIN;
            uint8_t ui8EGAIN;
        } sreg;

        uint32_t ui32devparam;
        uint32_t ui32delay;
        uint32_t ui32lux;
    } sals;
};

#endif
