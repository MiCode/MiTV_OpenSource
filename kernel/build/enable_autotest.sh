#!/bin/bash

sh scripts/config --enable CONFIG_MP_POTENTIAL_BUG
sh scripts/config --enable CONFIG_DPM_WATCHDOG
sh scripts/config --set-val CONFIG_DPM_WATCHDOG_TIMEOUT 2
