#ifndef _INPUT_MISC_H
#define _INPUT_MISC_H
struct block_key
{
	char name[30];
	u16 type;
	u16 keycode;
	u32 scancode;
	struct list_head list; /* kernel's list structure */
};

struct ir_key_map{
	char name[30];
	u32 scancode;
	u16 keycode;
	u16 keycode_new;
	struct list_head list; /* kernel's list structure */
};
#endif
