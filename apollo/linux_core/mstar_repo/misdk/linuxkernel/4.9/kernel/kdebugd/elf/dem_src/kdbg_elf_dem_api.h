/*
 *  kernel/kdebugd/aop/kdbg_elf_dem_api.h
 *
 *  Implementation of memory leak and shutdown mode
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2009-12-31  Created by gaurav.j.
 *
 */

#include <kdebugd/kdebugd.h>

 /* demangle the mangled name */
int kdbg_elf__sym_demangle(char *buff, char *new_buff, int buf_len);
