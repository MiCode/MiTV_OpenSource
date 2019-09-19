/*
 *  kernel/kdebugd/elf/kdbg_elf_sym_debug.h
 *
 *  Implementation of memory leak and shutdown mode
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2010-04-04  Created by gaurav.j.
 *
 */
#ifndef _SYM_DEBUG_H
#define _SYM_DEBUG_H  1

/* Selectively define AOP_DEBUG_ON to 1 in each source file that
 * requires the trace print macro, before include kdbg_elf_sym_debug.h header
 * file. In addition to it, each source file may use AOP_DEBUG_ON to
 * implement its internal debugging code (e.g. special internal test
 * menu, for example). This header file should be included only in
 * source code files (.c) */

/* trace on/off flag:  Enable it separetly in .c files only not here */
/* #define AOP_DEBUG_ON  0 */

#if defined(SYM_DEBUG_ON) && (SYM_DEBUG_ON != 0)
#define sym_printk(fmt, args...) do { \
					PRINT_KD("ELF: Tr: %s " fmt, __FUNCTION__ , ##args); \
				} while (0)
#else
#define sym_printk(fmt, ...) do { } while (0)
#endif /* AOP_DEBUG_ON */

#define sym_errk(fmt, args...) PRINT_KD("ELF: Err: %s:%d: %s()  " fmt, \
				__FILE__, __LINE__, __FUNCTION__ , ##args)

#endif /* #ifndef _SYM_DEBUG_H */
