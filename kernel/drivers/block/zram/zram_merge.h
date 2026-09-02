#ifndef _ZRAM_MERGE_H
#define _ZRAM_MERGE_H
#ifdef CONFIG_MP_ZSM
#include <linux/delay.h>

#ifdef CONFIG_MP_ZSM_DEBUG
#define TV_print(x...)           \
    do {                        \
            pr_err("TV-Log>> "x);         \
    } while (0)
#endif

static int zsm_test_link_flag(struct zram *zram, u32 index,
            enum zsm_flags flag);
static void zsm_set_link_flag(struct zram *zram, u32 index,
            enum zsm_flags flag);
static void zsm_clear_link_flag(struct zram *zram, u32 index,
            enum zsm_flags flag);
static struct zram_table_entry *search_node_in_zram_tree(
        struct zram *zram,
        struct zram_table_entry *input_node, u32 *parent_idx_ret,
		int *new_is_left, unsigned char *match_content,
		struct idx_rb_root *tree_root);
static int remove_node_from_zram_tree(struct zram *zram, u32 index,
		struct idx_rb_root *tree_root);
#ifdef CONFIG_MP_ZSM_DEBUG
static void zsm_print_flags(struct zram *zram, u32 index);
#endif

static __always_inline int zsm_test_link_flag(struct zram *zram, u32 index,
            enum zsm_flags flag)
{
    return zram->table[index].crc_link_node.flags_current & BIT(flag);
}

static __always_inline void zsm_init_tree_lock(zsm_tree_lock *lock_ptr)
{
    spin_lock_init(lock_ptr);
}

static __always_inline void zsm_lock_tree(zsm_tree_lock *lock_ptr)
{
	spin_lock(lock_ptr);
}

static __always_inline void zsm_unlock_tree(zsm_tree_lock *lock_ptr)
{
	spin_unlock(lock_ptr);
}

static const char * const zsm_supported_algos[] = {
    "lzo",
#if IS_ENABLED(CONFIG_MP_MZCCMDQ_HW)
    "mzc",
#endif
#if IS_ENABLED(CONFIG_MP_MZCCMDQ_HYBRID_HW)
    "mzc_hybrid",
#endif
};

static void zsm_validate_algo(struct zram *zram)
{
    int i = 0;
    bool known_algorithm = false;
    if (zram->zsm_on) {
        for (; i < ARRAY_SIZE(zsm_supported_algos); i++) {
            if (!strcmp(zram->compressor, zsm_supported_algos[i])) {
                known_algorithm = true;
                break;
            }
        }
        if (!known_algorithm) {
            zram->zsm_on = false;
            pr_alert("[ZRAM] ZSM does not support [%s], disable ZSM.\n",
					zram->compressor);
        }
    }
}

static __always_inline void zsm_set_link_flag(struct zram *zram, u32 index,
            enum zsm_flags flag)
{
	zram->table[index].crc_link_node.flags_current |= BIT(flag);
}

static __always_inline void zsm_clear_link_flag(struct zram *zram,
		u32 index, enum zsm_flags flag)
{
	zram->table[index].crc_link_node.flags_current &= ~BIT(flag);
}

static __always_inline u32 zsm_get_crc_next(struct zram *zram, u32 index)
{
	return zram->table[index].crc_link_node.same_crc_next;
}

static __always_inline void zsm_set_crc_next(struct zram *zram, u32 index,
		u32 next_idx)
{
	zram->table[index].crc_link_node.same_crc_next = next_idx;
}


static __always_inline u32 zsm_entry_to_index(struct zram *zram,
        struct zram_table_entry *entry)
{
    /* pointer substraction is the offset */
    return (entry - &zram->table[0]);
}

#ifdef CONFIG_MP_ZSM_DEBUG
static void zsm_print_history(struct zram *zram, u32 index);
static void zsm_print_flags(struct zram *zram, u32 index)
{
	char *false_sym = "___";
	pr_alert("idx=%u; pid=%d; handle=%lu; comp_len=%lu;\n"
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
		"%s | %s | %s | %s | %s | %s | %s | %s | %s; (end)\n",
#else
		"%s | %s | %s | %s | %s | %s | %s | %s; (end)\n",
#endif
		index, current->pid, zram->table[index].handle,
		zram_get_obj_size(zram, index),
		zsm_test_link_flag(zram, index, ZRAM_RB_NODE) ? "_RB" : false_sym,
		zsm_test_link_flag(zram, index, ZRAM_FIRST_NODE) ? "FIR" : false_sym,
		zsm_test_link_flag(zram, index, ZRAM_ZSM_DONE_NODE) ? "DON" : false_sym,
		zram_test_flag(zram, index, ZRAM_SAME) ? "SAM" : false_sym,
		zram_test_flag(zram, index, ZRAM_WB) ? "_WB" : false_sym,
		zram_test_flag(zram, index, ZRAM_UNDER_WB) ? "UWB" : false_sym,
		zram_test_flag(zram, index, ZRAM_HUGE) ? "HUG" : false_sym,
		zram_test_flag(zram, index, ZRAM_IDLE) ? "IDL" : false_sym
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
		, zram_test_flag(zram, index, ZRAM_IS_MZC) ? "MZC" : false_sym
#endif
		);
	zsm_print_history(zram, index);
}

static void zsm_print_list(struct zram *zram, u32 start_index)
{
	u32 next_idx, current_idx;
	/* print the index */
	current_idx = start_index;
	next_idx = zsm_get_crc_next(zram, current_idx);
	printk(KERN_CONT "%6s: ", "___IDX");
	printk(KERN_CONT "%6u->", current_idx);
	while (next_idx != start_index) {
		current_idx = next_idx;
		printk(KERN_CONT "%6u->", current_idx);
		next_idx = zsm_get_crc_next(zram, current_idx);
		if (current_idx == next_idx) {
			current_idx = next_idx;
			printk(KERN_CONT "%6u->(recur-break)\n", current_idx);
			zsm_print_flags(zram, start_index);
			zsm_print_flags(zram, current_idx);
			break;
		}
	}
	printk(KERN_CONT "end\n");
	/* print the ZRAM_RB_NODE */
	current_idx = start_index;
	next_idx = zsm_get_crc_next(zram, current_idx);
	printk(KERN_CONT "%6s: ", "____RB");
	printk(KERN_CONT "%6d->",
			zsm_test_link_flag(zram, current_idx, ZRAM_RB_NODE));
	while (next_idx != start_index) {
		current_idx = next_idx;
		printk(KERN_CONT "%6d->",
				zsm_test_link_flag(zram, current_idx, ZRAM_RB_NODE));
		next_idx = zsm_get_crc_next(zram, current_idx);
		if (current_idx == next_idx) {
			current_idx = next_idx;
			printk(KERN_CONT "%6u->(recur-break)\n",
					zsm_test_link_flag(zram, current_idx, ZRAM_RB_NODE));
			break;
		}
	}
	printk(KERN_CONT "end\n");
	/* print the ZRAM_FIRST_NODE */
	current_idx = start_index;
	next_idx = zsm_get_crc_next(zram, current_idx);
	printk(KERN_CONT "%6s: ", "_FIRST");
	printk(KERN_CONT "%6d->",
			zsm_test_link_flag(zram, current_idx, ZRAM_FIRST_NODE));
	while (next_idx != start_index) {
		current_idx = next_idx;
		printk(KERN_CONT "%6d->",
				zsm_test_link_flag(zram, current_idx, ZRAM_FIRST_NODE));
		next_idx = zsm_get_crc_next(zram, current_idx);
		if (current_idx == next_idx) {
			current_idx = next_idx;
			printk(KERN_CONT "%6u->(recur-break)\n",
					zsm_test_link_flag(zram, current_idx, ZRAM_FIRST_NODE));
			break;
		}
	}
	printk(KERN_CONT "end\n");
	/* print the ZRAM_ZSM_DONE_NODE */
	current_idx = start_index;
	next_idx = zsm_get_crc_next(zram, current_idx);
	printk(KERN_CONT "%6s: ", "ZSDONE");
	printk(KERN_CONT "%6d->",
			zsm_test_link_flag(zram, current_idx, ZRAM_ZSM_DONE_NODE));
	while (next_idx != start_index) {
		current_idx = next_idx;
		printk(KERN_CONT "%6d->",
				zsm_test_link_flag(zram, current_idx, ZRAM_ZSM_DONE_NODE));
		next_idx = zsm_get_crc_next(zram, current_idx);
		if (current_idx == next_idx) {
			current_idx = next_idx;
			printk(KERN_CONT "%6u->(recur-break)\n",
					zsm_test_link_flag(zram, current_idx, ZRAM_ZSM_DONE_NODE));
			break;
		}
	}
	printk(KERN_CONT "end\n");
	TV_print("********************************\n");
}

/* This is also called by zsm_print_flags(). */
static void zsm_record_history(struct zram *zram, u32 index, char *msg)
{
	int i = ZSM_DEBUG_HISTORY_CNT - 1;
	for (; i > 0; i--) {
		zram->table[index].history_len[i] =
			zram->table[index].history_len[i-1];
		zram->table[index].history_pid[i] =
			zram->table[index].history_pid[i-1];
		zram->table[index].history_handle[i] =
			zram->table[index].history_handle[i-1];
		zram->table[index].history_zsm_flags[i] =
			zram->table[index].history_zsm_flags[i-1];
		zram->table[index].history_zram_flags[i] =
			zram->table[index].history_zram_flags[i-1];
		memcpy(zram->table[index].history_msg[i],
				zram->table[index].history_msg[i-1], ZSM_DEBUG_HISTROY_MSG_SIZE);
	}
	/* comp_len */
	zram->table[index].history_len[0] = zram_get_obj_size(zram, index);

	/* pid */
	zram->table[index].history_pid[0] = current->pid;

	/* handle */
	zram->table[index].history_handle[0] = zram->table[index].handle;

	/* zsm flags */
	zram->table[index].history_zsm_flags[0] = zram->table[index].
		crc_link_node.flags_current;

	/* zram flags */
	zram->table[index].history_zram_flags[0] = zram->table[index].flags;

	/* msg */
	int len = strlen(msg);
	char buf[ZSM_DEBUG_HISTROY_MSG_SIZE] = {'\0'};
	len = len > ZSM_DEBUG_HISTROY_MSG_SIZE ? ZSM_DEBUG_HISTROY_MSG_SIZE:len;
	memcpy(buf, msg, len);
	memcpy(zram->table[index].history_msg[0], buf, ZSM_DEBUG_HISTROY_MSG_SIZE);
}

static int test_history_zsm_flags(struct zram *zram, u32 index,
		enum zsm_flags flag, int history_index)
{
    return zram->table[index].history_zsm_flags[history_index] & BIT(flag);
}

static int test_history_zram_flags(struct zram *zram, u32 index,
		enum zram_pageflags flag, int history_index)
{
    return zram->table[index].history_zram_flags[history_index] & BIT(flag);
}

static void zsm_print_history(struct zram *zram, u32 index)
{
	int i;
	char *false_sym = "___";
	for (i = 0; i < ZSM_DEBUG_HISTORY_CNT; i++) {
		pr_alert("History[%d] of idx=%u: %17s; pid=%d; handle=%lu; "
				"comp_len=%lu; --> \n ZSM: %s | %s | %s; "
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
				"ZRAM: %s | %s | %s | %s | %s | %s; "
#else
				"ZRAM: %s | %s | %s | %s | %s; "
#endif
				"(end)\n",
			i, index, zram->table[index].history_msg[i],
			zram->table[index].history_pid[i],
			zram->table[index].history_handle[i],
			zram->table[index].history_len[i],
			/* ZSM flags */
			test_history_zsm_flags(zram, index, ZRAM_RB_NODE, i) ? " RB" :
				false_sym,
			test_history_zsm_flags(zram, index, ZRAM_FIRST_NODE, i) ? "FIR" :
				false_sym,
			test_history_zsm_flags(zram, index, ZRAM_ZSM_DONE_NODE, i) ? "DONE" :
				false_sym,
			/* ZRAM flags */
			test_history_zram_flags(zram, index, ZRAM_SAME, i) ? "SAME" :
				false_sym,
			test_history_zram_flags(zram, index, ZRAM_WB, i) ? "WB" :
				false_sym,
			test_history_zram_flags(zram, index, ZRAM_UNDER_WB, i) ? "UD_WB" :
				false_sym,
			test_history_zram_flags(zram, index, ZRAM_HUGE, i) ? "HUGE" :
				false_sym,
			test_history_zram_flags(zram, index, ZRAM_IDLE, i) ? "IDLE" :
				false_sym
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
			, test_history_zram_flags(zram, index, ZRAM_IS_MZC, i) ? "MZC" :
				false_sym
#endif
			);
	}
	pr_alert("***************************************\n");
}
#endif


/* Must hold the index lock */
static __always_inline void zram_zsm_entry_init(struct zram *zram, u32 index,
		u32 checksum, unsigned int comp_len)
{
#ifdef CONFIG_MP_ZSM_DEBUG
	if (zram->table[index].crc_link_node.flags_current != 0) {
		TV_print("[ZSM] index=%u, zsm_flags = %u which should be zero.\n",
				index, zram->table[index].crc_link_node.flags_current);
		zsm_print_flags(zram, index);
		zsm_print_list(zram, index);
		BUG_ON(1);
	}
	if (zsm_get_crc_next(zram, index) != index) {
		TV_print("[ZSM] index=%u which is not properly freed.\n", index);
		zsm_print_flags(zram, index);
		zsm_print_list(zram, index);
		BUG_ON(1);
	}
#endif
	zram->table[index].checksum = checksum;
	idx_rb_node_init(&(zram->table[index]._idx_rb_node));
	zram_set_obj_size(zram, index, comp_len);
#ifdef CONFIG_MP_ZSM_DEBUG
	zram->table[index].copy_count = 0;
#endif
}

/* Use indices as single circular linked list for crc_list */
static void insert_node_to_zram_list(struct zram *zram, u32 new_index,
		u32 old_index)
{
    u32 old_next_idx;
	old_next_idx = zsm_get_crc_next(zram, old_index);
	zsm_set_crc_next(zram, new_index, old_next_idx);
	zsm_set_crc_next(zram, old_index, new_index);
}
#ifdef CONFIG_MP_MZCCMDQ_HW_SPLIT
extern DEFINE_PER_CPU(bool, dec_is_mzc);
#endif
static struct zram_table_entry *search_node_in_zram_list(
        struct zram *zram, struct zram_table_entry *input_node,
        struct zram_table_entry *found_node,
        unsigned char *match_content, u32 comp_len)
{
#ifdef CONFIG_MP_ZSM_DEBUG
    if (found_node == NULL) {
        TV_print("found_node is NULL\n");
        BUG_ON(1);
    }
    u32 i, j;
    i = 0;
	j = 0;
#endif
    u32 current_idx;
    u32 rb_node_idx;
    u32 one_node_in_list;
    struct zram_table_entry *current_node;
    unsigned char *cmem;
    int compare_count;
    int ret;
#if defined(CONFIG_MP_MZCCMDQ_HW) && defined(CONFIG_ARM)
	unsigned long input_phys_addr;
#endif

    /* The head of the crc_list */
    rb_node_idx = zsm_entry_to_index(zram, found_node);
	current_idx = zsm_get_crc_next(zram, rb_node_idx);
    if (rb_node_idx == current_idx) {
        one_node_in_list = 1;
#ifdef CONFIG_MP_ZSM_DEBUG
		if (!zsm_test_link_flag(zram, current_idx, ZRAM_RB_NODE)) {
			TV_print("[ZSM] one_node_in_list, and it is not RB.\n");
			zsm_print_flags(zram, current_idx);
			zsm_print_list(zram, current_idx);
			BUG_ON(1);
		}
#endif
    }
    compare_count = 0;
    /* circular linked list, go back to rb_node means ending */
    while ((current_idx != rb_node_idx) || one_node_in_list) {
        one_node_in_list = 0;
        current_node = &zram->table[current_idx];
        if ((comp_len == zram_get_obj_size(zram, current_idx)) &&
				zsm_test_link_flag(zram, current_idx, ZRAM_FIRST_NODE)) {
			/* this is the potential same page that we want */
            if (likely(zsm_test_link_flag(zram, current_idx, ZRAM_ZSM_DONE_NODE)
						&& (current_node->handle != 0))) {
				/* the page is ready to be memcmp */
#ifdef CONFIG_MP_MZCCMDQ_HW_SPLIT
                bool *is_mzc = &get_cpu_var(dec_is_mzc);
                *is_mzc = 0;
                put_cpu_var(dec_is_mzc);
#endif
				cmem = zs_map_object(zram->mem_pool, current_node->handle,
						ZS_MM_RO);
				/* memcmp for the same checksum and same size page */
                ret = memcmp(cmem, match_content,
                        zram_get_obj_size(zram, zsm_entry_to_index(zram,
								input_node)));
                compare_count++;
                if (ret == 0) {
                    zs_unmap_object(zram->mem_pool, current_node->handle);
                    return current_node;
                }
                zs_unmap_object(zram->mem_pool, current_node->handle);
            } else {
				/*
				 * This might happen in multithread zram_bvec_write() before the
				 * handle & ZRAM_ZSM_DONE_NODE are assigned, but the zsm info is
				 * inserted.
				 * This makes the same page be not merged.
				 * This is not a bug. Do nothing.
				 */
            }
        }
#ifdef CONFIG_MP_ZSM_DEBUG
		if (i > 16384) {
			u32 next_idx;
			next_idx = zsm_get_crc_next(zram, current_idx);
			TV_print("[ZSM] might be bugs which search too much time! "
					"current_idx=%u; next_idx=%u; "
					"current FIRST=%d; next FIRST=%d; "
					"current comp_len=%d; comp_len=%d; "
					"rb_idx=%d; \n",
					current_idx, next_idx,
					zsm_test_link_flag(zram, current_idx, ZRAM_FIRST_NODE),
					zsm_test_link_flag(zram, next_idx, ZRAM_FIRST_NODE),
					zram_get_obj_size(zram, current_idx), comp_len,
					rb_node_idx);
			zsm_print_flags(zram, current_idx);
			zsm_print_list(zram, current_idx);
			zsm_print_flags(zram, rb_node_idx);
			zsm_print_list(zram, rb_node_idx);
			BUG_ON(1);
		}
		i++;
#endif
		current_idx = zsm_get_crc_next(zram, current_idx);
    }
    return NULL;
}

static struct zram_table_entry *search_node_in_zram_tree(
        struct zram *zram,
        struct zram_table_entry *input_node, u32 *parent_idx_ret,
		int *new_is_left, unsigned char *match_content,
		struct idx_rb_root *tree_root)
{
	u32 parent_idx, new_idx;
	parent_idx = tree_root->idx_rb_node_index;
	new_idx = parent_idx;
    struct zram_table_entry *current_node;
#ifdef CONFIG_MP_ZSM_DEBUG
    if (input_node == NULL) {
        pr_err("[ZSM][search_node_in_zram_tree] input_node is NULL\n");
        return NULL;
    }
	int i = 0;
#endif
    while (IDX_EXISTS(new_idx)) {
		current_node = idx_rb_entry(tree_root, new_idx,
				struct zram_table_entry);
#ifdef CONFIG_MP_ZSM_DEBUG
		i++;
        if (!zsm_test_link_flag(zram, new_idx, ZRAM_RB_NODE)) {
            TV_print("The index=%u, size=%u in rb_tree is not rb_node "
					"at iter=%d\n",
                    new_idx, zram_get_obj_size(zram, new_idx), i);
			zsm_print_list(zram, new_idx);
            BUG_ON(1);
        }
#endif
		parent_idx = new_idx;
        if (input_node->checksum > current_node->checksum) {
            new_idx = IDX_NODE(tree_root, new_idx)->rb_right_index;
			*new_is_left = 0;
        } else if (input_node->checksum < current_node->checksum) {
            new_idx = IDX_NODE(tree_root, new_idx)->rb_left_index;
			*new_is_left = 1;
        } else {
			size_t input_size = zram_get_obj_size(zram, zsm_entry_to_index(zram,
						input_node));
			size_t current_size = zram_get_obj_size(zram,
					zsm_entry_to_index(zram, current_node));
            if (input_size > current_size) {
				new_idx = IDX_NODE(tree_root, new_idx)->rb_right_index;
				*new_is_left = 0;
			} else if (input_size < current_size) {
				new_idx = IDX_NODE(tree_root, new_idx)->rb_left_index;
				*new_is_left = 1;
			} else {
                return current_node;
			}
        }
    }
	*parent_idx_ret = parent_idx;
    return NULL;
}


static u32 zsm_get_crc_list_head(struct zram *zram, u32 index, u32 *prev_idx) {
    u32 start_idx = index;
    u32 current_idx = start_idx;
	u32 next_idx = current_idx;
	u32 impossible_index = BIT(ZRAM_1024MB_PAGE_BITS);
	*prev_idx = impossible_index;
#ifdef CONFIG_MP_ZSM_DEBUG
	int i = 0;
#endif
    do {
        next_idx = zsm_get_crc_next(zram, current_idx);
		if (zsm_test_link_flag(zram, next_idx, ZRAM_RB_NODE)) {
			*prev_idx = current_idx;
		}
        if (zsm_test_link_flag(zram, current_idx, ZRAM_RB_NODE)) {
            break;
        }
		current_idx = next_idx;
#ifdef CONFIG_MP_ZSM_DEBUG
		if (i > 16384) {
			TV_print("[ZSM] Bug at line %d for index=%u\n", __LINE__,
					current_idx);
			zsm_print_flags(zram, current_idx);
			zsm_print_list(zram, current_idx);
			BUG_ON(1);
		}
		i++;
#endif
    } while (start_idx != current_idx);
#ifdef CONFIG_MP_ZSM_DEBUG
	i = 0;
#endif
	if (*prev_idx == impossible_index) {
		/* prev_idx is the last in the list */
		u32 tmp_current_idx = current_idx;
		next_idx = zsm_get_crc_next(zram, tmp_current_idx);
		while (!zsm_test_link_flag(zram, next_idx, ZRAM_RB_NODE)) {
			tmp_current_idx = next_idx;
			next_idx = zsm_get_crc_next(zram, tmp_current_idx);
#ifdef CONFIG_MP_ZSM_DEBUG
			if (i > 16384) {
				TV_print("[ZSM] Bug at line %d for index=%u\n", __LINE__,
						next_idx);
				zsm_print_flags(zram, next_idx);
				zsm_print_list(zram, next_idx);
				BUG_ON(1);
			}
			i++;
#endif
		}
		*prev_idx = tmp_current_idx;
	}
#ifdef CONFIG_MP_ZSM_DEBUG
    if (!zsm_test_link_flag(zram, current_idx, ZRAM_RB_NODE)) {
        TV_print("Return head index=%u, which is not RB_NODE.\n", current_idx);
        zsm_print_list(zram, current_idx);
        BUG_ON(1);
    }
	if (*prev_idx == impossible_index) {
		TV_print("Got the wrong prev_idx.\n");
        BUG_ON(1);
	}
#endif
    return current_idx;
}

static int remove_node_from_zram_list(struct zram *zram, u32 index)
{
    int i;
    i = 0;
    /* Use rb_node to get the head of list */
    struct zram_table_entry *input_node;

    input_node = &(zram->table[index]);
    /* Start to iterate the list from rb_node */
    u32 current_idx;
    u32 next_idx;
    u32 prev_idx;
    current_idx = zsm_get_crc_list_head(zram, index, &prev_idx);
#ifdef CONFIG_MP_ZSM_DEBUG
	u32 first_idx;
	first_idx = current_idx;
#endif
	next_idx = zsm_get_crc_next(zram, current_idx);
	/* index is the RB_NODE which requires different handling for prev_idx */
	if (index == current_idx) {
		/* do nothing, next_idx & prev_idx are ready. */
#ifdef CONFIG_MP_ZSM_DEBUG
		if (!zsm_test_link_flag(zram, index, ZRAM_RB_NODE)) {
			TV_print("[ZSM] [Error] index=%u must be ZRAM_RB_NODE\n", index);
			BUG_ON(1);
		}
		if (!zsm_test_link_flag(zram, index, ZRAM_FIRST_NODE)) {
			TV_print("[ZSM] [Error] index=%u must be RB | FIRST\n", index);
			BUG_ON(1);
		}
		first_idx = current_idx;
#endif
	} else {
		/* index is not the RB_NODE */
		while (current_idx != index) {
			/* try to find the input index */
#ifdef CONFIG_MP_ZSM_DEBUG
			i++;
			if (i >= 16384) {
				pr_err(
					"[ZRAM]can't find zram->table[%u].size=%d; "
					"chunksum=%u; current_idx=%u\n", index, 
					zram_get_obj_size(zram, index), zram->table[index].checksum,
					current_idx);
				zsm_print_list(zram, index);
				zsm_print_flags(zram, index);
				pr_err("current_idx=%u\n", current_idx);
				zsm_print_list(zram, current_idx);
				zsm_print_flags(zram, current_idx);
				BUG_ON(1);
			}
#endif
			prev_idx = current_idx;
			current_idx = next_idx;
#ifdef CONFIG_MP_ZSM_DEBUG
			if (zsm_test_link_flag(zram, current_idx, ZRAM_FIRST_NODE)) {
				first_idx = current_idx;
			}
#endif
			next_idx = zsm_get_crc_next(zram, current_idx);
		}
	}

	if (!zsm_test_link_flag(zram, index, ZRAM_FIRST_NODE) ||
			!zsm_test_link_flag(zram, next_idx, ZRAM_FIRST_NODE)) {
		/* found the index (page) duplicated at least once */
#ifdef CONFIG_MP_ZSM_DEBUG
		u32 tmp_idx;
		if (zsm_test_link_flag(zram, index, ZRAM_FIRST_NODE)) {
			zram->table[next_idx].copy_count =
				zram->table[index].copy_count - 1;
			tmp_idx = next_idx;
		} else {
			zram->table[first_idx].copy_count -= 1;
			tmp_idx = first_idx;
		}
		if (zram->table[tmp_idx].copy_count < 0) {
			pr_err("[ZRAM]Warning !!count < 0, idx=%u; count=%d for removing"
					" idx=%u\n",
					tmp_idx, zram->table[tmp_idx].copy_count, index);
			zsm_print_list(zram, tmp_idx);
			zsm_print_flags(zram, tmp_idx);
			BUG_ON(1);
		}
#endif
		if (zsm_test_link_flag(zram, index, ZRAM_FIRST_NODE)) {
			/* clear must after set for the situation that next_idx == index */
			zsm_set_link_flag(zram, next_idx, ZRAM_FIRST_NODE);
		}
		/* reconnect the node before and after the target */
		zsm_set_crc_next(zram, prev_idx, next_idx);
		zsm_clear_link_flag(zram, index, ZRAM_FIRST_NODE);
		zsm_set_crc_next(zram, index, index);
#ifdef CONFIG_MP_ZSM_STAT
		size_t obj_size = zram_get_obj_size(zram, index);
        if (unlikely(obj_size >= huge_class_size)) {
            atomic64_sub((u64)obj_size, &zram->stats.zsm_saved_nCompr_sz);
            atomic64_dec(&zram->stats.zsm_saved_nCompr_pg_cnt);
        } else {
            atomic64_sub((u64)obj_size, &zram->stats.zsm_saved_Compr_sz);
            atomic64_dec(&zram->stats.zsm_saved_Compr_pg_cnt);
        }
#endif
        return 1;
	}
    /* can't found the same page content */
#ifdef CONFIG_MP_ZSM_DEBUG
    if (!zsm_test_link_flag(zram, index, ZRAM_FIRST_NODE)) {
        pr_err("[ZSM] ERROR index is not ZRAM_FIRST_NODE, but it is duplicated."
                " index=%u\n", index);
		zsm_print_list(zram, index);
		zsm_print_flags(zram, index);
        BUG_ON(1);
    }
    if (zram->table[index].copy_count != 0) {
        pr_err("[ZRAM]ERROR !!index != next_index and count != 0; "
                "index=%u; count=%u\n ", index,
                zram->table[index].copy_count);
		zsm_print_list(zram, index);
		zsm_print_flags(zram, index);
        BUG_ON(1);
    }
#endif
	/* reconnect the node before and after the target */
	zsm_set_crc_next(zram, prev_idx, next_idx);
	zsm_clear_link_flag(zram, index, ZRAM_FIRST_NODE);
	zsm_set_crc_next(zram, index, index);
    return 0;
}

static int remove_node_from_zram_tree(struct zram *zram, u32 index,
		struct idx_rb_root *tree_root)
{
    int ret;
    if (zsm_test_link_flag(zram, index, ZRAM_ZSM_DONE_NODE)) {
        zsm_clear_link_flag(zram, index, ZRAM_ZSM_DONE_NODE);
    }
    /* if it is rb node,
     * choose other node from list and replace original node. */
    if (zsm_test_link_flag(zram, index, ZRAM_RB_NODE)) {
        /* found next node in list */
        if (index != zsm_get_crc_next(zram, index)) {
			u32 next_idx;
			next_idx = zsm_get_crc_next(zram, index);
            ret = remove_node_from_zram_list(zram, index);
            zsm_set_link_flag(zram, next_idx, ZRAM_RB_NODE);
            idx_rb_replace_node(index, next_idx, tree_root);
			zsm_clear_link_flag(zram, index, ZRAM_RB_NODE);
#ifdef CONFIG_MP_ZSM_DEBUG
			zsm_record_history(zram, index, "Replaced. RB");
#endif
            return ret;
        }
        /* if no other node can be found in list,
         * just remove node from rb tree and free handle */
        if (zsm_test_link_flag(zram, index, ZRAM_FIRST_NODE)) {
            zsm_clear_link_flag(zram, index, ZRAM_FIRST_NODE);
        } else {
            pr_err("[ZRAM]ERROR !!ZRAM_RB_NODE's flag != ZRAM_FIRST_NODE "
                    "index %x\n ",
                    index);
            BUG_ON(1);
        }
#ifdef CONFIG_MP_ZSM_DEBUG
        if (tree_root == NULL) {
            pr_err("[ZSM] ERROR!! tree_root is NULL\n");
        }
#endif
		idx_rb_erase(index, tree_root);
		zsm_clear_link_flag(zram, index, ZRAM_RB_NODE);
		zsm_clear_link_flag(zram, index, ZRAM_FIRST_NODE);
#ifdef CONFIG_MP_ZSM_DEBUG
		zsm_record_history(zram, index, "Erased. RB");
#endif
        return 0;
    }
    ret = remove_node_from_zram_list(zram, index);
#ifdef CONFIG_MP_ZSM_DEBUG
	zsm_record_history(zram, index, "Rm.List. RB");
#endif
    return ret;
}

/* return 0 if not found, 1 for found */
static int search_page_in_zsm_info(struct bio_vec *bvec, struct zram *zram,
		u32 index, unsigned char *match_content, u32 checksum,
		unsigned int comp_len, struct page *page, struct idx_rb_root *tree_root,
		struct zsm_connect *search_results, size_t size_threshold)
{
    struct zram_table_entry *current_node = NULL;
    struct zram_table_entry *node_in_list = NULL;
    u32 parent_idx;
	int new_is_left;
    struct zram_table_entry *input_node;
	parent_idx = tree_root->idx_rb_node_index;
	new_is_left = 0;

    input_node = &(zram->table[index]);
    current_node = search_node_in_zram_tree(zram, input_node, &parent_idx,
			&new_is_left, match_content, tree_root);
    /* found node in zram_tree */
    if (NULL != current_node) {
#ifdef CONFIG_MP_ZSM_DEBUG
        if (!zsm_test_link_flag(zram, zsm_entry_to_index(zram, current_node),
                    ZRAM_RB_NODE)) {
            pr_err("[ZRAM]ERROR !!found wrong rb node 0x%p, rb_index=%u; "
                    "for index=%u\n",
                    (void *)current_node, zsm_entry_to_index(zram,
                        current_node), index);
            BUG_ON(1);
        }
#endif
        /* check if there is any other node in this position. */
		if (unlikely(comp_len > size_threshold)) {
			match_content = kmap_atomic(page);
		}
        node_in_list = search_node_in_zram_list(zram, input_node,
                current_node, match_content, comp_len);
		if (unlikely(comp_len > size_threshold)) {
			kunmap_atomic(match_content);
		}
        /* found the same node in list */
        if (NULL != node_in_list) {
            /* insert node after the found node */
#ifdef CONFIG_MP_ZSM_DEBUG
            if (!zsm_test_link_flag(zram, zsm_entry_to_index(zram, current_node),
                        ZRAM_FIRST_NODE)) {
                pr_err("[ZRAM]ERROR !!found wrong first node 0x%p, rb_idx=%u, list_idx=%u\n",
                        (void *)node_in_list, zsm_entry_to_index(zram,
                            current_node), zsm_entry_to_index(zram,
                                node_in_list));
                BUG_ON(1);
            }
#endif
			search_results->found_in_tree_idx = zsm_entry_to_index(zram,
					current_node);
			search_results->found_in_list_idx = zsm_entry_to_index(zram,
					node_in_list);
            return 1;
        }
        /* 
		 * Can't find the SAME node in list, but the crc_list was created.
		 * Must insert before a ZRAM_FIRST_NODE, but after ZRAM_RB_NODE 
		 * to maintain the sub lists. 
		 */
		search_results->found_in_tree_idx = zsm_entry_to_index(zram,
				current_node);
    } else {
        /* insert node into rb tree */
		search_results->parent_idx = parent_idx;
		search_results->new_is_left = new_is_left;
    }
    return 0;
}

static __always_inline void zsm_connect_init(struct zsm_connect *res)
{
	res->found_in_tree_idx = IDX_RB_RESERVED_INDEX;
	res->found_in_list_idx = IDX_RB_RESERVED_INDEX;
	res->parent_idx = IDX_RB_RESERVED_INDEX;
	res->new_is_left = 0;
}

static __always_inline int zsm_select_compr_tree(unsigned int comp_len)
{
	/* split trees for every 256 bytes into ZSM_Compr_TREE_COUNT trees */
	int tree_idx = (comp_len & ~PAGE_SIZE) >> 8;
    return tree_idx;
}

static void insert_page_in_zsm_info(struct bio_vec *bvec, struct zram *zram,
		u32 index, unsigned char *match_content, u32 checksum,
		unsigned int comp_len, struct page *page, struct idx_rb_root *tree_root,
		struct zsm_connect *search_results)
{
#ifdef CONFIG_MP_ZSM_DEBUG
	int i = 0;
#endif
	if (IDX_EXISTS(search_results->found_in_tree_idx)) {
		if (IDX_EXISTS(search_results->found_in_list_idx)) {
			/* found samepage */
#ifdef CONFIG_MP_ZSM_DEBUG
			if(zram->table[search_results->found_in_list_idx].handle == 0) {
				TV_print("Get handle=%lu from index=%u\n",
						zram->table[search_results->found_in_list_idx].handle,
						search_results->found_in_list_idx);
				zsm_print_history(zram, search_results->found_in_list_idx);
				BUG_ON(1);
			}
#endif
			zram_slot_lock(zram, index);
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
			if (zram_test_flag(zram, search_results->found_in_list_idx,
						ZRAM_IS_MZC)) {
				zram_set_flag(zram, index, ZRAM_IS_MZC);
			} else {
				zram_clear_flag(zram, index, ZRAM_IS_MZC);
			}
#endif
			zram_set_handle(zram, index, zram->table[
					search_results->found_in_list_idx].handle);
            insert_node_to_zram_list(zram, index,
					search_results->found_in_list_idx);
            zsm_set_link_flag(zram, index, ZRAM_ZSM_DONE_NODE);
#ifdef CONFIG_MP_ZSM_DEBUG
			zsm_record_history(zram, index, "Insert. DUP.");
#endif
			zram_slot_unlock(zram, index);
            /* found the same node and add ref count */
#ifdef CONFIG_MP_ZSM_DEBUG
			zram->table[search_results->found_in_list_idx].copy_count += 1;
#endif
#ifdef CONFIG_MP_ZSM_STAT
            if (unlikely(comp_len >= huge_class_size)) {
                atomic64_add((u64)comp_len, &zram->stats.zsm_saved_nCompr_sz);
                atomic64_inc(&zram->stats.zsm_saved_nCompr_pg_cnt);
            } else {
                atomic64_add((u64)comp_len, &zram->stats.zsm_saved_Compr_sz);
                atomic64_inc(&zram->stats.zsm_saved_Compr_pg_cnt);
            }
#endif
		} else {
			/* found rb_node, but no same page */
			u32 current_idx, next_idx;
			current_idx = search_results->found_in_tree_idx;
			next_idx = zsm_get_crc_next(zram, current_idx);
			while(!zsm_test_link_flag(zram, next_idx, ZRAM_FIRST_NODE)) {
				current_idx = next_idx;
				next_idx = zsm_get_crc_next(zram, current_idx);
#ifdef CONFIG_MP_ZSM_DEBUG
				if (i > 16384) {
					TV_print("[ZSM] Bug at line %d for index=%u\n", __LINE__,
							next_idx);
					zsm_print_flags(zram, next_idx);
					zsm_print_list(zram, next_idx);
					BUG_ON(1);
				}
				i++;
#endif
			}
			insert_node_to_zram_list(zram, index, current_idx);
			zsm_set_link_flag(zram, index, ZRAM_FIRST_NODE);
#ifdef CONFIG_MP_ZSM_DEBUG
			zsm_record_history(zram, index, "Insert. FIR.");
#endif
		}
	} else {
        zsm_set_link_flag(zram, index, ZRAM_FIRST_NODE);
        zsm_set_link_flag(zram, index, ZRAM_RB_NODE);
		/* 
		 * pages as rb_node in rb_tree do not need to 
		 * insert_node_to_zram_list().
		 * It is already pointed it itself during entry init or "remove"
		 * for the index.
		 */
        idx_rb_link_node(index, search_results->parent_idx,
				search_results->new_is_left, tree_root);
		idx_rb_insert_color(index, tree_root);
#ifdef CONFIG_MP_ZSM_DEBUG
		zsm_record_history(zram, index, "Insert. RB|FIR.");
#endif
	}
}


static int zsm_info_cleanup(struct zram *zram, u32 index,
		zsm_tree_lock tree_lock_Compr[], struct idx_rb_root tree_root_Compr[],
		zsm_tree_lock *tree_lock_nCompr, struct idx_rb_root *tree_root_nCompr)
{
	int search_ret = -1;
	zsm_tree_lock *tree_lock;
	struct idx_rb_root *tree_root;
	unsigned int comp_len = zram_get_obj_size(zram, index);
	if (!zsm_test_link_flag(zram, index, ZRAM_ZSM_DONE_NODE)) {
		/* zram page that from zs_malloc slow-path, free it */
		return 0;
	}
	if (likely(comp_len != PAGE_SIZE)) {
		tree_lock = &tree_lock_Compr[zsm_select_compr_tree(comp_len)];
		tree_root = &tree_root_Compr[zsm_select_compr_tree(comp_len)];
	} else {
		tree_lock = tree_lock_nCompr;
		tree_root = tree_root_nCompr;
	}

	zsm_lock_tree(tree_lock);
	search_ret = remove_node_from_zram_tree(zram, index, tree_root);
	zsm_unlock_tree(tree_lock);
	return search_ret;
}

#endif
#endif
