#ifndef _LINUX_WHITELIST_H
#define _LINUX_WHITELIST_H

#include <linux/types.h>
#include <asm-generic/current.h>

#define BP_FN_LEN		256
#define BP_STATUS_DISABLE	0
#define BP_STATUS_ENABLE	1
#define BP_STATUS_NEVER		2


int __init white_list_init (void);
int is_bypass(int, kgid_t, kuid_t);
int bypass_chk (kgid_t c_gid, kuid_t c_uid, char *exec_name, loff_t size);
#define BYPASS_NUM 0

struct bypass_entry {
	gid_t gid;
	uid_t uid;
	loff_t fsize;
	char exec_name[BP_FN_LEN];
	uint32_t __entry_status;
};

extern struct bypass_entry * bypass_table;

#endif /* ifndef _LINUX_WHITELIST_H */
