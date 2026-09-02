/*
 * MTK keypad driver
 *
 * Copyright (C) 2019 MTK Co.Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/input/mtksar-keypad.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/timekeeping.h>
#include <linux/freezer.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#if defined(CONFIG_MSTAR_PM)
#include <include/mdrv_pm.h>
#endif

#define MTK_KEYPAYD_ROW_MAX             2
#define MTK_KEYPAYD_COL_MAX             8
#define SAR_CTRL                        (0x0 << 1)
#define SAR_CTRL_SINGLE_MODE            (1 << 4)
#define SAR_CTRL_DIG_FREERUN_MODE       (1 << 5)
#define SAR_CTRL_ATOP_FREERUN_MODE      (1 << 9)
#define SAR_CTRL_8_CHANNEL              (1 << 0xb)
#define SAR_CTRL_LOAD_EN                (1 << 0xe)

#define SAR_INT_MASK                    (0x28 << 1)
#define SAR_AISEL_MODE                  (0x22 << 1)
#define SAR_VOL_REF                     (0x32 << 1)
#define SAR_CH_UP                       (0x40 << 1)
#define SAR_CH_LOW                      (0x60 << 1)
#define SAR_INT_STS                     (0x2e << 1)
#define SAR_INT_CLS                     (0x2a << 1)
#define SAR_ADC_CH                      (0x80 << 1)
#define SAR_CH_MAX                       4
#define SAR_KEY_MAX                      8
#define SAR_CHANNEL_USED_MAX             2
#define LONG_PRESS_TIMEOUT               20  //100m*20 = 2 secs

#define PM_IRQ_MASK                      (0x10 << 1)
#define PM_SAR_WK_EN                     (1 << 1)
#define TASK_POLLING_TIMES               100    //msec
#define PM_SLEEP_INTERRUPT_NUM           0x21
#define DBG_KEYPAD_INFO                  0
#define MTK_KEYPAD_DEVICE_NAME           "MTK TV KEYPAD"
#define INPUTID_VENDOR 0x3697UL;
#define INPUTID_PRODUCT 0x0002;
#define INPUTID_VERSION 0x0002;
#define RELEASE_KEY                      0xFF
#define DEFAULT_DEBOUNCE                 200  //200msec
#define DEFAULT_DEBOUNCE_MAX             1000  //1sec
#define RELEASE_ADC                      0x3F0

enum mtk_keypad_type {
    KEYPAD_TYPE_MTK,
    KEYPAD_TYPE_CUSTOMIZED0,
};

typedef struct{
    unsigned int keycode[SAR_CHANNEL_USED_MAX];
    unsigned int keythresh_vol[SAR_CHANNEL_USED_MAX];
}list_key;


struct mtk_keypad {
    struct input_dev *input_dev;
    struct platform_device *pdev;
    void __iomem *base;
    wait_queue_head_t wait;
    //list_key scanmap[SAR_KEY_MAX];
    bool stopped;
    bool wake_enabled;
    int irq;
    enum mtk_keypad_type type;
    unsigned int row_state[SAR_CHANNEL_USED_MAX];
    unsigned int *keycodes;
    unsigned int *keythreshold;
    unsigned int  chanel;
    unsigned int  low_bound;
    unsigned int  high_bound;
    unsigned int  key_num;
    unsigned int  key_adc;
    unsigned int  prekey[SAR_CHANNEL_USED_MAX];
    unsigned int  pressed[SAR_CHANNEL_USED_MAX];
    unsigned int  ch_detected;
    unsigned int  repeat_count;
    unsigned int  debounce_time;
    bool no_autorepeat;
    unsigned int  release_adc;
    struct notifier_block reboot_notifier;
};

struct mi_get_keypad_buf {
    unsigned char mi_ch;
    unsigned char mi_keynum;
    unsigned char mi_UppBnd;
    unsigned char mi_LoBnd;
    unsigned char mi_Threshold[SAR_KEY_MAX];
    u16 mi_Keycode[SAR_KEY_MAX];
};
static list_key scanmap[SAR_KEY_MAX];
static unsigned int numkey[SAR_CHANNEL_USED_MAX];
static unsigned volatile int keystored_t[SAR_CHANNEL_USED_MAX];
static unsigned volatile char _uprelease[SAR_CHANNEL_USED_MAX];
static unsigned volatile char _downtrig[SAR_CHANNEL_USED_MAX];
static ktime_t _start_time[SAR_CHANNEL_USED_MAX];
static uintptr_t ginputaddr[SAR_CHANNEL_USED_MAX];
static struct mtk_keypad *gkeypad;

static void mtk_keypad_map(struct mtk_keypad *keypad)
{
   unsigned int i;
   unsigned int j;
   unsigned int keynum;

   list_key tmp;
   list_key buffer[SAR_KEY_MAX+1];
   keynum = keypad->key_num;

   for(i =0 ;i <keynum ;i++)
   {
       buffer[i].keycode[keypad->chanel] = (keypad->keycodes)[i];
       buffer[i].keythresh_vol[keypad->chanel] =(keypad->keythreshold)[i];
   }

   for(i = 0;i<keynum;i++)
   {
       for(j = i;j<keynum;j++)
       {
           if(buffer[i].keythresh_vol[keypad->chanel] > buffer[j].keythresh_vol[keypad->chanel])
           {
               memcpy(&tmp,&buffer[i],sizeof(list_key));
               memcpy(&buffer[i],&buffer[j],sizeof(list_key));
               memcpy(&buffer[j],&tmp,sizeof(list_key));
           }
       }
   }

   for(i=0;i < keypad->key_num; i++)
   {
      if(i == (keypad->key_num - 1))
      {
           buffer[i+1].keythresh_vol[keypad->chanel] = keypad->release_adc;
      }
      buffer[i].keythresh_vol[keypad->chanel] = (buffer[i].keythresh_vol[keypad->chanel] + 
                                                 buffer[i+1].keythresh_vol[keypad->chanel])/2;
      scanmap[i].keycode[keypad->chanel] = buffer[i].keycode[keypad->chanel];
      scanmap[i].keythresh_vol[keypad->chanel] = buffer[i].keythresh_vol[keypad->chanel];
   }
}


static void mtk_keypad_scan(struct mtk_keypad *keypad,
                unsigned int *row_state)
{
    unsigned int scancnt;

    for(scancnt = 0; scancnt < (numkey[keypad->ch_detected]); scancnt++)
    {
        if((keypad->key_adc) < (scanmap[scancnt].keythresh_vol[keypad->ch_detected]))
        {
            row_state[keypad->ch_detected] = scanmap[scancnt].keycode[keypad->ch_detected];
#if DBG_KEYPAD_INFO
            printk("\033[45;37m keypad_kernel ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,
                     scanmap[scancnt].keycode[keypad->ch_detected]);
#endif
            scancnt = 0;
            break;
        }
    }
    if(scancnt == numkey[keypad->ch_detected])
    {
        row_state[keypad->ch_detected] = RELEASE_KEY;
    }
}

static struct task_struct *t_keypad_tsk;

static int t_keypad_thread(void* arg)
{
    struct mtk_keypad *keypad_t = arg;
    struct input_dev *input_dev_t = keypad_t->input_dev;
    unsigned int ctrl_t,adc_val_t,ch_offset_t;
    unsigned int row_state_t[SAR_CHANNEL_USED_MAX];


    while(1)
    {
        wait_event_freezable(keypad_t->wait, _downtrig[keypad_t->ch_detected] == 1 );
        /*Reduce CPU Loading*/
        msleep(TASK_POLLING_TIMES);
        ctrl_t = readl(keypad_t->base + SAR_CTRL);
        ctrl_t |= SAR_CTRL_LOAD_EN;
        writel(ctrl_t, keypad_t->base + SAR_CTRL);
        ch_offset_t = (keypad_t->ch_detected) << 2;
        adc_val_t = readl(keypad_t->base + SAR_ADC_CH + ch_offset_t);
        keypad_t->key_adc = adc_val_t;
        mtk_keypad_scan(keypad_t,row_state_t);
        keystored_t[keypad_t->ch_detected] = row_state_t[keypad_t->ch_detected];

        if(_downtrig[keypad_t->ch_detected] == 1)
        {
           if((keystored_t[keypad_t->ch_detected] != RELEASE_KEY)&&
                                                           (_uprelease[keypad_t->ch_detected]))
           {
                keypad_t->prekey[keypad_t->ch_detected] = keystored_t[keypad_t->ch_detected];
                keypad_t->pressed[keypad_t->ch_detected] = 1;
                _uprelease[keypad_t->ch_detected] = 0;
                input_event(ginputaddr[keypad_t->ch_detected],
                                           EV_KEY, keystored_t[keypad_t->ch_detected], 1);
                input_sync(ginputaddr[keypad_t->ch_detected]);
                _start_time[keypad_t->ch_detected] = ktime_get();
           }
        }

        if (_uprelease[keypad_t->ch_detected] == 0)
        {
            if((keystored_t[keypad_t->ch_detected] == keypad_t->prekey[keypad_t->ch_detected])
                  &&(keypad_t->pressed[keypad_t->ch_detected])
                       &&(keystored_t[keypad_t->ch_detected]!=RELEASE_KEY))
            {
                 //printk("\033[45;37m  ""%s %d::   \033[0m\n",__FUNCTION__,keypad_t->repeat_count);
                if(keypad_t->no_autorepeat == 0)
                {
                    if (ktime_ms_delta(ktime_get(),
                                _start_time[keypad_t->ch_detected]) > (keypad_t->debounce_time))
                    {
                        input_event(input_dev_t, EV_KEY, keystored_t[keypad_t->ch_detected], 2);
                        input_sync(keypad_t->input_dev);
                        _start_time[keypad_t->ch_detected] = ktime_get();
                    }
                }
                keypad_t->repeat_count ++;
            }
            else if((adc_val_t > scanmap[(keypad_t->key_num)-1].keythresh_vol
                             [keypad_t->ch_detected])&&(keypad_t->pressed[keypad_t->ch_detected]))
            {
                keypad_t->pressed[keypad_t->ch_detected] = 0;
                keypad_t->repeat_count = 0;
                input_event(ginputaddr[keypad_t->ch_detected],
                                              EV_KEY, keypad_t->prekey[keypad_t->ch_detected], 0);
                input_sync(ginputaddr[keypad_t->ch_detected]);
                keypad_t->prekey[keypad_t->ch_detected] = 0;
                _uprelease[keypad_t->ch_detected] = 1;
                _downtrig[keypad_t->ch_detected]= 0;
            }
            else /*two button pressed*/
            {   
                if((keystored_t[keypad_t->ch_detected]!=RELEASE_KEY)&&
                   ( keypad_t->pressed[keypad_t->ch_detected] ==1))
                {
                    /*Linux support repeat event for combination key*/
                    if(keypad_t->no_autorepeat == 0)
                    {
                        if (ktime_ms_delta(ktime_get(),  _start_time[keypad_t->ch_detected]) > 
                                                                         (keypad_t->debounce_time))
                        {
                            input_event(ginputaddr[keypad_t->ch_detected], EV_KEY,
                                                       keypad_t->prekey[keypad_t->ch_detected], 2);
                            input_sync(ginputaddr[keypad_t->ch_detected]);
                            _start_time[keypad_t->ch_detected] = ktime_get();
                        }
                    }
                }
            }
        }
    }
    return 0;
}

static irqreturn_t mtk_keypad_irq_thread(int irq, void *dev_id)
{

    struct mtk_keypad *keypad = dev_id;
    unsigned int int_sts;
    unsigned int int_ststmp;
    unsigned int ctrl,adc_val,ch_offset;
    unsigned int chanelcnt;
    unsigned int row_state[SAR_CHANNEL_USED_MAX];
    unsigned int keystored;
    unsigned int keycount_timeout;

    chanelcnt = 0 ;
#ifdef CONFIG_MSTAR_PM
    MDrv_PM_WakeIrqMask(0x00);
#endif
    ctrl = readl(keypad->base + SAR_CTRL);
    ctrl |= SAR_CTRL_LOAD_EN;

    int_sts = readl(keypad->base + SAR_INT_STS);
    int_ststmp = int_sts;
#if DBG_KEYPAD_INFO
    printk("\033[45;37m  ""%s %x::   \033[0m\n",__FUNCTION__,int_ststmp);
#endif
    while(int_ststmp)
    {
        if((int_sts & (1 << chanelcnt)) == 0)
        {
            chanelcnt ++;
            int_ststmp >>= 1;
            continue;
        }
        int_ststmp >>= 1;
        ch_offset = chanelcnt << 2;
        writel(ctrl, keypad->base + SAR_CTRL);
        adc_val = readl(keypad->base + SAR_ADC_CH + ch_offset);
        writel(int_sts, keypad->base + SAR_INT_CLS);
        keypad->ch_detected = chanelcnt;
        /*Prevent AC Keypad debounce interrupt*/
        if(adc_val < keypad->release_adc)
        {
            _downtrig[keypad->ch_detected] = 1;
            wake_up(&keypad->wait);
        }
        chanelcnt = 0;
    }
#ifdef CONFIG_MSTAR_PM
    MDrv_PM_WakeIrqMask(0x02);
#endif
    return IRQ_HANDLED;
}

static void mtk_keypad_start(struct mtk_keypad *keypad)
{
    unsigned int val;

    /* Tell IRQ thread that it may poll the device. */
    keypad->stopped = false;

    /* Enable interrupt bits. */
    val = readl(keypad->base + SAR_CTRL);

    val &= ~SAR_CTRL_SINGLE_MODE;
    val |= SAR_CTRL_DIG_FREERUN_MODE;
    val |= SAR_CTRL_ATOP_FREERUN_MODE;
    val |= SAR_CTRL_8_CHANNEL;

    writel(val, keypad->base + SAR_CTRL);

    val = readl(keypad->base + SAR_INT_MASK);

#if DBG_KEYPAD_INFO
    printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,readl(keypad->base + SAR_CTRL));
    printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,keypad->chanel);
#endif

    val &= ~(1<<(keypad->chanel));
    writel(val,keypad->base + SAR_INT_MASK);

    val = readl(keypad->base + SAR_AISEL_MODE);
    val |= (1<<(keypad->chanel));
    writel(val,keypad->base + SAR_AISEL_MODE);

    val = keypad->high_bound;
    writel(val,keypad->base + SAR_CH_UP+(keypad->chanel << 2));

    val = keypad->low_bound;
    writel(val,keypad->base + SAR_CH_LOW+(keypad->chanel << 2));

}

static void mtk_keypad_stop(struct mtk_keypad *keypad)
{

}

static int mtk_keypad_open(struct input_dev *input_dev)
{
    struct mtk_keypad *keypad = input_get_drvdata(input_dev);

    gkeypad = input_get_drvdata(input_dev);
    mtk_keypad_start(keypad);
    mtk_keypad_map(keypad);
#ifdef CONFIG_MSTAR_PM
    MDrv_PM_WakeIrqMask(0x02);
#endif
    return 0;
}

static void mtk_keypad_close(struct input_dev *input_dev)
{
    struct mtk_keypad *keypad = input_get_drvdata(input_dev);

    mtk_keypad_stop(keypad);
}

#ifdef CONFIG_OF
static struct mtk_keypad_platdata *
mtk_keypad_parse_dt(struct device *dev)
{
    struct mtk_keypad_platdata *pdata;
    struct device_node *np = dev->of_node, *key_np;
    uint32_t *keythreshold;
    uint32_t *keyscancode;
    uint32_t keychanel;
    uint32_t keynum;
    uint32_t key_l;
    uint32_t key_h;
    uint32_t debouncetiming;
    uint32_t rst_timing;
    uint32_t  rst_key;
    uint32_t releaseadc;

    rst_timing = 0;

    if (!np) {
        dev_err(dev, "missing device tree data\n");
        return ERR_PTR(-EINVAL);
    }

    pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
    if (!pdata) {
        dev_err(dev, "could not allocate memory for platform data\n");
        return ERR_PTR(-ENOMEM);
    }

    of_property_read_u32(np, "mtk,keypad-chanel", &keychanel);
    of_property_read_u32(np, "mtk,keypad-num", &keynum);

    of_property_read_u32(np, "mtk,keypad-lowbd", &key_l);
    of_property_read_u32(np, "mtk,keypad-upwbd", &key_h);
    of_property_read_u32(np, "mtk,keypad-debounce", &debouncetiming);
    of_property_read_u32(np, "mtk,rst-timeout", &rst_timing);
    of_property_read_u32(np, "mtk,rst-key", &rst_key);
    of_property_read_u32(np, "mtk,keypad-releaseadc", &releaseadc);


#if DBG_KEYPAD_INFO
    printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,rst_key);
    printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,rst_timing);
    printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,keychanel);
    printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,keynum);
    printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,key_l);
    printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,key_h);
#endif

    if (keychanel > SAR_CH_MAX)
    {
        dev_err(dev, "number of keychanel not specified\n");
        return ERR_PTR(-EINVAL);
    }

    if ((!keynum)||(keynum > SAR_KEY_MAX))
    {
        dev_err(dev, "number of keynum not specified\n");
        return ERR_PTR(-EINVAL);
    }

    keythreshold = devm_kzalloc(dev, sizeof(uint32_t) * keynum, GFP_KERNEL);
    if (!keythreshold) {
        dev_err(dev, "could not allocate memory for keythreshold\n");
        return ERR_PTR(-ENOMEM);
    }
    pdata->threshold = keythreshold;

    keyscancode = devm_kzalloc(dev, sizeof(uint32_t) * keynum, GFP_KERNEL);
    if (!keyscancode) {
        dev_err(dev, "could not allocate memory for keythreshold\n");
        return ERR_PTR(-ENOMEM);
    }
    pdata->sarkeycode = keyscancode;

    pdata->chanel = keychanel;
    pdata->key_num = keynum;
    pdata->low_bound = key_l;
    pdata->high_bound = key_h;
    pdata->debounce_time = debouncetiming;
    pdata->reset_timeout = rst_timing;
    pdata->reset_key = rst_key;

    for_each_child_of_node(np, key_np) {
        u32 key_code, th_vol;
        of_property_read_u32(key_np, "linux,code", &key_code);
        of_property_read_u32(key_np, "keypad,threshold", &th_vol);
        *keythreshold++ = th_vol;
        *keyscancode++ = key_code;
    }

    pdata->wakeup = of_property_read_bool(np, "wakeup-source") ||
            /* legacy name */
            of_property_read_bool(np, "linux,input-wakeup");

    if (of_get_property(np, "linux,input-no-autorepeat", NULL))
    {
        pdata->no_autorepeat = true;
    }
    else
    {
        pdata->no_autorepeat = 0;
    }

    if (of_get_property(np, "mtk,keypad-releaseadc", NULL))
    {
       pdata->release_adc = releaseadc;
    }
    else
    {
       pdata->release_adc = 0x3FF;
    }

    return pdata;
}
#else
static struct mtk_keypad_platdata *
mtk_keypad_parse_dt(struct device *dev)
{
    dev_err(dev, "no platform data defined\n");

    return ERR_PTR(-EINVAL);
}
#endif

static int keypad_reboot_notify(struct notifier_block* nb, unsigned long event, void *data)
{
    int ret = NOTIFY_OK;

    struct mtk_keypad *keypad = container_of(nb, struct mtk_keypad, reboot_notifier);
    SAR_RegCfg buf;
    unsigned char keynum_tmp;

    memset(&buf, 0, sizeof(SAR_RegCfg));
    buf.u8SARChID = keypad->chanel;
    buf.tSARChBnd.u8UpBnd = 0xFF;
    //buf.tSARChBnd.u8LoBnd = (keypad->low_bound) >> 2;
    buf.u8KeyLevelNum = keypad->key_num;

    printk("keypad_reboot_notify event=%lu\n", event);
    switch (event)
    {
        case SYS_POWER_OFF:
             for(keynum_tmp = 0 ; keynum_tmp < (keypad->key_num); keynum_tmp++)
             {
                 buf.u8KeyCode[keynum_tmp]= scanmap[keynum_tmp].keycode[keypad->chanel];
                 buf.u8KeyThreshold[keynum_tmp] = (scanmap[keynum_tmp].keythresh_vol[keypad->chanel] >> 2);
             }
             if(buf.u8SARChID == 0)
             {
                 MDrv_PM_Write_Key(PM_KEY_SAR0, (char *)&buf, sizeof(SAR_RegCfg));
             }
             else
             {
                 MDrv_PM_Write_Key(PM_KEY_SAR1, (char *)&buf, sizeof(SAR_RegCfg));
             }
            break;

        default:
            break;
    }

    return ret;
}

static struct notifier_block pm_reboot_notifier = {
    .notifier_call = keypad_reboot_notify,
};

static int mtk_keypad_probe(struct platform_device *pdev)
{
    const struct mtk_keypad_platdata *pdata;
    struct mtk_keypad *keypad;
    struct resource *res;
    struct input_dev *input_dev;
    int error;
    int keycnt;


    pdata = dev_get_platdata(&pdev->dev);
    if (!pdata) {
        pdata = mtk_keypad_parse_dt(&pdev->dev);
        if (IS_ERR(pdata))
            return PTR_ERR(pdata);
    }

    keypad = devm_kzalloc(&pdev->dev, sizeof(*keypad),GFP_KERNEL);

    //Apply input_dev structure
    input_dev = devm_input_allocate_device(&pdev->dev);
    if (!keypad || !input_dev)
        return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res)
        return -ENODEV;

    keypad->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
    if (!keypad->base)
        return -EBUSY;

    keypad->input_dev = input_dev;
    keypad->pdev = pdev;
    keypad->stopped = true;
    keypad->keycodes = pdata->sarkeycode;
    keypad->keythreshold = pdata->threshold;
    keypad->chanel = pdata->chanel;
    keypad->low_bound = pdata->low_bound;
    keypad->high_bound = pdata->high_bound;
    keypad->key_num = pdata->key_num;
    keypad->no_autorepeat = pdata->no_autorepeat;
    keypad->debounce_time = pdata->debounce_time;
    keypad->release_adc = pdata->release_adc;

    if((keypad->debounce_time < DEFAULT_DEBOUNCE)||(keypad->debounce_time > DEFAULT_DEBOUNCE_MAX))
    {
        keypad->debounce_time = DEFAULT_DEBOUNCE;
    }

#if DBG_KEYPAD_INFO
    for(keycnt=0;keycnt < keypad->key_num; keycnt++) {
        printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,(keypad->keycodes)[keycnt]);
    }
    for(keycnt=0;keycnt < keypad->key_num; keycnt++) {
        printk("\033[45;37m  ""%s[%x] ::   \033[0m\n",__FUNCTION__ ,(keypad->keythreshold)[keycnt]);
    }
#endif
    init_waitqueue_head(&keypad->wait);

    if (pdev->dev.of_node)
        keypad->type = of_device_is_compatible(pdev->dev.of_node,
                    "mtk,hq-keypad");
    else
        keypad->type = platform_get_device_id(pdev)->driver_data;

    //input_dev->name = pdev->name;
    input_dev->name = MTK_KEYPAD_DEVICE_NAME;
    input_dev->id.bustype = BUS_HOST;
    input_dev->dev.parent = &pdev->dev;
    input_dev->id.vendor = INPUTID_VENDOR;
    input_dev->id.product = INPUTID_PRODUCT;
    input_dev->id.version = INPUTID_VERSION;

    input_dev->open = mtk_keypad_open;
    input_dev->close = mtk_keypad_close;

    _uprelease[keypad->chanel] = 1;
    _downtrig[keypad->chanel] = 0;
    ginputaddr[keypad->chanel] = input_dev;
    _start_time[keypad->chanel] = 0;
    numkey[keypad->chanel] = pdata->key_num;
    //input_set_capability(input_dev, EV_MSC, MSC_SCAN);
    for(keycnt=0;keycnt < keypad->key_num; keycnt++) {
        input_set_capability(input_dev, EV_KEY, (keypad->keycodes)[keycnt]);
    }

    input_set_drvdata(input_dev, keypad);
    keypad->irq = PM_SLEEP_INTERRUPT_NUM;

    //keypad->irq = platform_get_irq(pdev, 0);
    //printk("\033[45;37m  ""@@@@@@%s[%x] ::   \033[0m\n",__FUNCTION__ ,keypad->irq);
    if (keypad->irq < 0) {
        error = keypad->irq;
        goto err_handle;
    }

    error = devm_request_irq(&pdev->dev, keypad->irq, mtk_keypad_irq_thread,
                                                     IRQF_SHARED, dev_name(&pdev->dev), keypad);


    if (error) {
        dev_err(&pdev->dev, "failed to register keypad interrupt\n");
        goto err_handle;
    }

    platform_set_drvdata(pdev, keypad);
    keypad->reboot_notifier.notifier_call = keypad_reboot_notify;
    register_reboot_notifier(&keypad->reboot_notifier);

    /*Register Input node*/
    error = input_register_device(keypad->input_dev);
    if (error) {
        dev_err(&pdev->dev, "failed to input keypad device\n");
        goto err_handle;
    }

    /*patch for add_timer issue*/
    t_keypad_tsk = kthread_create(t_keypad_thread, keypad, "Keypad_Check");
    kthread_bind(t_keypad_tsk, 0);
    if (IS_ERR(t_keypad_tsk)) {
            printk("create kthread for keypad observation fail\n");
                error = PTR_ERR(t_keypad_tsk);
                t_keypad_tsk = NULL;
                goto err_handle;
    }else
    wake_up_process(t_keypad_tsk);
    /*patch for add_timer issue*/

    if (pdev->dev.of_node) {
        devm_kfree(&pdev->dev, (void *)pdata);
    }

    return 0;

err_handle:
    return error;
}

static int mtk_keypad_remove(struct platform_device *pdev)
{
    struct mtk_keypad *keypad = platform_get_drvdata(pdev);

    pm_runtime_disable(&pdev->dev);
    device_init_wakeup(&pdev->dev, 0);

    input_unregister_device(keypad->input_dev);

    return 0;
}

int mi_keypad_ini_fetch(struct mi_get_keypad_buf *ini_fetch)
{
    unsigned char keynum_tmp;
    MDrv_PM_WakeIrqMask(0x00);
    if(!gkeypad)
    {
        printk("\033[45;37m  ""NULL gkeypad \033[0m\n");
        return 1;
    }
    gkeypad->chanel = ini_fetch->mi_ch;
    gkeypad->high_bound = (ini_fetch->mi_UppBnd);
    gkeypad->low_bound  = (ini_fetch->mi_LoBnd);
    gkeypad->high_bound = ((gkeypad->high_bound)<<2)|0x03;
    gkeypad->low_bound  = ((gkeypad->low_bound)<<2)|0x03;
    gkeypad->key_num = ini_fetch->mi_keynum;
#if DBG_KEYPAD_INFO
    printk("\033[45;37m  ""ini_fetch->high_bound:%x\033[0m\n",ini_fetch->mi_UppBnd);
    printk("\033[45;37m  ""ini_fetch->low_bound :%x\033[0m\n",ini_fetch->mi_LoBnd);
    printk("\033[45;37m  ""gkeypad->chanel:%x\033[0m\n",gkeypad->chanel);
    printk("\033[45;37m  ""gkeypad->high_bound:%x\033[0m\n",gkeypad->high_bound);
    printk("\033[45;37m  ""gkeypad->low_bound :%x\033[0m\n",gkeypad->low_bound);
    printk("\033[45;37m  ""gkeypad->key_num :%x\033[0m\n",gkeypad->key_num);
#endif

    ginputaddr[ini_fetch->mi_ch] = gkeypad->input_dev;
    for(keynum_tmp = 0 ; keynum_tmp < (gkeypad->key_num); keynum_tmp++)
    {
        (gkeypad->keycodes)[keynum_tmp] = (ini_fetch->mi_Keycode)[keynum_tmp];
        (gkeypad->keythreshold)[keynum_tmp] = ((ini_fetch->mi_Threshold)[keynum_tmp])<<2;
        input_set_capability(ginputaddr[ini_fetch->mi_ch], EV_KEY,(gkeypad->keycodes)[keynum_tmp]);
    }

    mtk_keypad_start(gkeypad);
    mtk_keypad_map(gkeypad);

    MDrv_PM_WakeIrqMask(0x02);
    return 0;

}
EXPORT_SYMBOL(mi_keypad_ini_fetch);


#ifdef CONFIG_PM_SLEEP
static void mtk_keypad_toggle_wakeup(struct mtk_keypad *keypad,
                     bool enable)
{

}

static int mtk_keypad_suspend(struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct mtk_keypad *keypad = platform_get_drvdata(pdev);
    struct input_dev *input_dev = keypad->input_dev;
    SAR_RegCfg buf;
    unsigned char keynum_tmp;


    mutex_lock(&input_dev->mutex);

    if (input_dev->users)
        mtk_keypad_stop(keypad);

    memset(&buf, 0, sizeof(SAR_RegCfg));
    buf.u8SARChID = keypad->chanel;
    buf.tSARChBnd.u8UpBnd = 0xFF;
    //buf.tSARChBnd.u8LoBnd = (keypad->low_bound) >> 2;
    buf.u8KeyLevelNum = keypad->key_num;
    for(keynum_tmp = 0 ; keynum_tmp < (keypad->key_num); keynum_tmp++)
    {
        buf.u8KeyCode[keynum_tmp]= scanmap[keynum_tmp].keycode[keypad->chanel];
        buf.u8KeyThreshold[keynum_tmp] = (scanmap[keynum_tmp].keythresh_vol[keypad->chanel] >> 2);
    }
    if(buf.u8SARChID == 0)
    {
        MDrv_PM_Write_Key(PM_KEY_SAR0, (char *)&buf, sizeof(SAR_RegCfg));
    }
    else
    {
        MDrv_PM_Write_Key(PM_KEY_SAR1, (char *)&buf, sizeof(SAR_RegCfg));
    }

    disable_irq(PM_SLEEP_INTERRUPT_NUM);
    mtk_keypad_toggle_wakeup(keypad, true);

    mutex_unlock(&input_dev->mutex);

    return 0;
}

static int mtk_keypad_resume(struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev);
    struct mtk_keypad *keypad = platform_get_drvdata(pdev);
    struct input_dev *input_dev = keypad->input_dev;

    mutex_lock(&input_dev->mutex);

    mtk_keypad_toggle_wakeup(keypad, false);

    mtk_keypad_start(keypad);
    mtk_keypad_map(keypad);
    enable_irq(PM_SLEEP_INTERRUPT_NUM);
#ifdef CONFIG_MSTAR_PM
    MDrv_PM_WakeIrqMask(0x02);
#endif

    mutex_unlock(&input_dev->mutex);

    return 0;
}
#endif

static const struct dev_pm_ops mtk_keypad_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(mtk_keypad_suspend, mtk_keypad_resume)
};

#ifdef CONFIG_OF
static const struct of_device_id mtk_keypad_dt_match[] = {
    { .compatible = "mtk,hq-keypad" },
    { .compatible = "mtk,customized0-keypad" },
    {},
};
MODULE_DEVICE_TABLE(of, mtk_keypad_dt_match);
#endif

static const struct platform_device_id mtk_keypad_driver_ids[] = {
    {
        .name       = "mtk-keypad",
        .driver_data    = KEYPAD_TYPE_MTK,
    }, {
        .name       = "mtkcus0-keypad",
        .driver_data    = KEYPAD_TYPE_CUSTOMIZED0,
    },
    { },
};
MODULE_DEVICE_TABLE(platform, mtk_keypad_driver_ids);

static struct platform_driver mtk_keypad_driver = {
    .probe      = mtk_keypad_probe,
    .remove     = mtk_keypad_remove,
    .driver     = {
        .name   = "mtk-keypad",
        .of_match_table = of_match_ptr(mtk_keypad_dt_match),
        .pm = &mtk_keypad_pm_ops,
    },
    .id_table   = mtk_keypad_driver_ids,
};
module_platform_driver(mtk_keypad_driver);

MODULE_DESCRIPTION("Mtk keypad driver");
MODULE_AUTHOR("AUTHOR");
MODULE_LICENSE("GPL");
