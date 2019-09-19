#!/bin/bash

# kdebug
sh scripts/config --enable CONFIG_REDLION_DEBUG
sh scripts/config --enable CONFIG_SERIAL_INPUT_MANIPULATION
# stack tracer
#sh scripts/config --enable CONFIG_FTRACE
#sh scripts/config --enable CONFIG_STACK_TRACER
#sh scripts/config --enable CONFIG_DEBUG_KERNEL
#sh scripts/config --enable CONFIG_DEBUG_STACK_USAGE
# kmemleak
sh scripts/config --enable CONFIG_DEBUG_KERNEL
sh scripts/config --enable CONFIG_DEBUG_KMEMLEAK
# perf
sh scripts/config --enable CONFIG_PERF_EVENTS
sh scripts/config --enable CONFIG_PERF_COUNTERS
sh scripts/config --enable CONFIG_PROFILING
sh scripts/config --enable CONFIG_OPROFILE
# telnet
sh scripts/config --enable CONFIG_TTY
sh scripts/config --enable CONFIG_UNIX98_PTYS
# DS5
sh scripts/config --enable CONFIG_GENERIC_TRACER
sh scripts/config --enable CONFIG_CONTEXT_SWITCH_TRACER
sh scripts/config --enable CONFIG_PROFILING
sh scripts/config --enable CONFIG_HIGH_RES_TIMERS
sh scripts/config --enable CONFIG_PERF_EVENTS
sh scripts/config --enable CONFIG_HW_PERF_EVENTS
sh scripts/config --enable CONFIG_DEBUG_INFO
sh scripts/config --enable CONFIG_CPU_FREQ
sh scripts/config --disable CONFIG_REDLION_DEBUG
sh scripts/config --enable CONFIG_ENABLE_DEFAULT_TRACERS
#KASAN
sh scripts/config --enable CONFIG_KASAN
sh scripts/config --enable CONFIG_HAVE_ARCH_KASAN
