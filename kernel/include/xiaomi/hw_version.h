#ifndef __HW_VERSION_H
#define __HW_VERSION_H

#define CROODS_WD1T "wd1t"

typedef enum {
	HW_WD1T,
	HW_MAX
} XiaomiHwVerType;

XiaomiHwVerType get_xiaomi_hw_version(void);
char *get_xiaomi_hw_version_str(void);

bool get_xiaomi_dap_disable(void);

#endif /* __HW_VERSION_H */
