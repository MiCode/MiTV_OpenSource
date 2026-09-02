/*
 * MTK Keypad platform data definitions
 *
 * Copyright (C) 2019 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MTK_KEYPAD_H
#define __MTK_KEYPAD_H

#include <linux/input/matrix_keypad.h>


/**
 * struct mtk_keypad_platdata - Platform device data for Samsung Keypad.
 * @wakeup: controls whether the device should be set up as wakeup source.
 *
 * Initialisation data specific to either the machine or the platform
 * for the device driver to use or call-back when configuring gpio.
 */
struct mtk_keypad_platdata {
    bool wakeup;
    unsigned int *threshold;
    unsigned int *sarkeycode;
    unsigned int  chanel;
    unsigned int  low_bound;
    unsigned int  high_bound;
    unsigned int  key_num;
    unsigned int  debounce_time;
    bool no_autorepeat;
    unsigned int  reset_timeout;
    unsigned int  reset_key;
    unsigned int  release_adc;
};

#endif /* __MTK_KEYPAD_H */
