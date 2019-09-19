/*
 * eHCI host controller driver
 *
 * Copyright (C) 2012~2017 MStar Inc.
 *
 *
 * Date: 2015.12.11
 *   1. software patch of VFall state machine mistake is removed, all chips
 *      run with kernel 4.4.3 should be VFall hardware ECO
 */

#ifndef _EHCI_MSTAR_H
#define _EHCI_MSTAR_H

#define EHCI_MSTAR_VERSION "20190717"

#include <ehci-mstar-40932.h>

#ifdef USB_MSTAR_BDMA
void m_BDMA_write(unsigned int, unsigned int);
void set_64bit_OBF_cipher(void);
int get_64bit_OBF_cipher(void);
#endif

#ifndef MSTAR_WIFI_FAST_CONNECT
#define MSTAR_WIFI_FAST_CONNECT
#endif

#endif	/* _EHCI_MSTAR_H */
