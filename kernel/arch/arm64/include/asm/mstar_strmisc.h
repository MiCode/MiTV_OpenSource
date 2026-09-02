#ifndef _MSTAR_STRMISC_H_
#define _MSTAR_STRMISC_H_

#if defined(CONFIG_ARM64)
extern ptrdiff_t mstar_pm_base;
#define RIU_MAP (mstar_pm_base+0x200000)
#else
#define RIU_MAP 0xFD200000
#endif

#define RIU     ((unsigned short volatile *) RIU_MAP)
#define RIU8    ((unsigned char  volatile *) RIU_MAP)

#define REG_XC_BASE                               (0x2F00)
#define XC_REG(addr)                               RIU[(addr<<1)+REG_XC_BASE]

#endif
