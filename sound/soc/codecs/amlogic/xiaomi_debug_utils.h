#ifndef __XIAOMI_DEBUG_UTILS_H
#define __XIAOMI_DEBUG_UTILS_H

extern char *get_reg_pos(const char *buf, const char *reg, size_t count);
extern int parse_value_by_length(const char *buf,
	unsigned char *dest, int length);

#endif
