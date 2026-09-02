#!/bin/bash

sh scripts/config --enable CONFIG_KASAN
sh scripts/config --set-val CONFIG_CONSOLE_LOGLEVEL_QUIET 7
sh scripts/config --set-val CONFIG_MESSAGE_LOGLEVEL_DEFAULT 0
