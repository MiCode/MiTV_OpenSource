/*
 * IDX_RB_TREE:
 * This is an index version of rb_tree in "lib/rbtree.c", instead of "pointer".
 * This also does not support atmoic rb tree operations due to the bitfields.
 */

/*
 * red-black trees properties:  http://en.wikipedia.org/wiki/Rbtree
 *
 *  1) A node is either red or black
 *  2) The root is black
 *  3) All leaves (NULL) are black
 *  4) Both children of every red node are black
 *  5) Every simple path from root to leaves contains the same number
 *     of black nodes.
 *
 *  4 and 5 give the O(log n) guarantee, since 4 implies you cannot have two
 *  consecutive red nodes in a path and every red node is therefore followed by
 *  a black. So if B is the number of black nodes on every simple path (as per
 *  5), then the longest possible path due to 4 is 2B.
 *
 *  We shall indicate color with case, where black nodes are uppercase and red
 *  nodes will be lowercase. Unknown color nodes shall be drawn as red within
 *  parentheses and have some accompanying text comment.
 */

#ifndef _IDX_RBTREE_H
#define _IDX_RBTREE_H
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/bitops.h>

#define IDX_RB_print(x...)           \
    do {                        \
            pr_err("IDX_RB>> "x);         \
    } while (0)

#define IDX_RB_print_cont(x...)           \
    do {                        \
		printk(KERN_CONT x); \
    } while (0)

#define IDX_RB_COLOR_BIT 1
#define IDX_RB_RED       0
#define IDX_RB_BLACK     1

#define IDX_RB_RESERVED_COUNT   1
#define IDX_RB_1024MB_MAX_BITS  18
#define IDX_RB_MAX_COUNT        BIT(IDX_RB_1024MB_MAX_BITS)
#define IDX_RB_MAX_INDEX        (IDX_RB_MAX_COUNT - 1)
#define IDX_RB_MAX_AVAIL_INDEX  (IDX_RB_MAX_INDEX - IDX_RB_RESERVED_COUNT)
/* IDX_RB_TREE uses bitfield, which cannot be done in atomic */
#define WRITE_ONCE_FOO(a, b)  (a = b)

enum idx_rb_reserved {
    /* reserved index for special purpose */
    idx_rb_last_avail_idx = IDX_RB_MAX_AVAIL_INDEX,
    idx_rb_unavail_idx,

    __END_IDX_RB_RESERVED
};

/* Used as the NULL to the pointer */
#define IDX_RB_RESERVED_INDEX   idx_rb_unavail_idx

/* 7 bytes */
struct idx_rb_node {
    u8 rb_color: IDX_RB_COLOR_BIT;
    u32 rb_parent_index: IDX_RB_1024MB_MAX_BITS;
    u32 rb_left_index: IDX_RB_1024MB_MAX_BITS;
    u32 rb_right_index: IDX_RB_1024MB_MAX_BITS;
} __attribute__((packed));

struct idx_rb_root {
    u32 idx_rb_node_index;
	void *table; /* the table of indices */
	unsigned long element_size; /* element size of table */
	/* relative address to the struct that contains member "idx_rb_node" */
	unsigned long idx_rb_offset;
};

#define IDX_RB_ROOT(table_in, type, idx_rb_member_name) (struct idx_rb_root) {\
	.idx_rb_node_index = IDX_RB_RESERVED_INDEX,\
	.table = (void *)table_in,\
	.element_size = sizeof(type),\
	.idx_rb_offset = offsetof(type, idx_rb_member_name)}

#ifndef CONFIG_MP_ZSM_DEBUG
#define IDX_NODE(root, index) (idx_to_entry(root, index))
#else
#define IDX_NODE(root, index) (idx_to_entry(root, index, __FILE__, __LINE__))
#endif
#define idx_rb_parent(root, index) (IDX_NODE(root, index)->rb_parent_index)
#define IDX_EXISTS(index)  (index < IDX_RB_RESERVED_INDEX)
#define idx_rb_entry(root, index, type) \
	(type *)((unsigned long)IDX_NODE(root, index) - root->idx_rb_offset)
#define __idx_color_is_black(color) (color == IDX_RB_BLACK)

static inline void idx_rb_node_init(struct idx_rb_node *node)
{
	node->rb_color = 0;
	node->rb_parent_index = IDX_RB_RESERVED_INDEX;
	node->rb_left_index = IDX_RB_RESERVED_INDEX;
	node->rb_right_index = IDX_RB_RESERVED_INDEX;
}

#ifndef CONFIG_MP_ZSM_DEBUG
static inline struct idx_rb_node *idx_to_entry(struct idx_rb_root *root,
		u32 index)
#else
static inline struct idx_rb_node *idx_to_entry(struct idx_rb_root *root,
		u32 index, char *file_name, u32 line_num)
#endif
{
	if (unlikely(!IDX_EXISTS(index))) {
#ifdef CONFIG_MP_ZSM_DEBUG
		IDX_RB_print("idx_to_entry() from %s: %u\n", file_name,line_num);
#endif
		return NULL;
	}
	return (struct idx_rb_node *)((unsigned long)root->table +
			root->element_size*index + root->idx_rb_offset);
}

static inline void idx_rb_set_parent(struct idx_rb_root *root,
		u32 rb_idx, u32 p_idx)        
{
	struct idx_rb_node *rb;
	rb = IDX_NODE(root, rb_idx);
	rb->rb_parent_index = p_idx;
	/* remain the same color */
}

static inline void idx_rb_set_black(struct idx_rb_root *root,
		u32 index)
{
	IDX_NODE(root, index)->rb_color |= IDX_RB_BLACK;
}

static inline void idx_rb_set_parent_color(struct idx_rb_root *root, u32 rb_idx,
		u32 p_idx, int color)
{
	struct idx_rb_node *rb;
	rb = IDX_NODE(root, rb_idx);
	rb->rb_parent_index = p_idx;
	rb->rb_color = color;
}

static inline u32 idx_rb_red_parent(struct idx_rb_root *root,
		u32 index)
{
    return IDX_NODE(root, index)->rb_parent_index;
}

static inline int idx_rb_is_black(struct idx_rb_root *root, u32 index)
{
	return __idx_color_is_black(IDX_NODE(root, index)->rb_color);
}

static inline int idx_rb_is_red(struct idx_rb_root *root, u32 index)
{
	return !idx_rb_is_black(root, index);
}

#ifdef CONFIG_MP_ZSM_DEBUG

#undef IDX_RB_DEBUG_PRINT_TREE

static void idx_rb_traverse_node(struct idx_rb_root *root, u32 index, unsigned int *results, unsigned int *result_index)
{
	if (!IDX_EXISTS(index)) {
		return;
	}
	/* inorder */
	idx_rb_traverse_node(root, IDX_NODE(root, index)->rb_left_index, results, result_index);
#ifdef IDX_RB_DEBUG_PRINT_TREE
	IDX_RB_print_cont("%u, ", index);
#endif
	if (results != NULL) {
		results[*result_index] = index;
		*result_index = *result_index + 1;
	}
	idx_rb_traverse_node(root, IDX_NODE(root, index)->rb_right_index, results, result_index);
}

static void idx_rb_traverse_tree(struct idx_rb_root *root)
{
	if (!IDX_EXISTS(root->idx_rb_node_index)) {
		IDX_RB_print("Empty idx_rb_tree.\n");
	} else {
		u32 foo;
		idx_rb_traverse_node(root, root->idx_rb_node_index, NULL, &foo);
	}
	IDX_RB_print_cont("Traverse done.\n");
}
#endif

static inline void
__idx_rb_change_child(u32 old_idx, u32 new_idx,	u32 parent_idx,
		struct idx_rb_root *root)
{
    if (IDX_EXISTS(parent_idx)) {
		struct idx_rb_node *parent = IDX_NODE(root, parent_idx);
        if (parent->rb_left_index == old_idx) {
            WRITE_ONCE_FOO(parent->rb_left_index, new_idx);
		} else {
            WRITE_ONCE_FOO(parent->rb_right_index, new_idx);
		}
    } else {
        WRITE_ONCE_FOO(root->idx_rb_node_index, new_idx);
	}
}

/*
 * Helper function for rotations:
 * - old's parent and color get assigned to new
 * - old gets assigned new as a parent and 'color' as a color.
 */
static inline void
__idx_rb_rotate_set_parents(u32 old_idx, u32 new_idx, struct idx_rb_root *root,
		int color)
{
    u32 parent_idx = idx_rb_parent(root, old_idx);
	struct idx_rb_node *new, *old;
	new = IDX_NODE(root, new_idx);
	old = IDX_NODE(root, old_idx);
    new->rb_color = old->rb_color;
    new->rb_parent_index = old->rb_parent_index;
    idx_rb_set_parent_color(root, old_idx, new_idx, color);
    __idx_rb_change_child(old_idx, new_idx, parent_idx, root);
}

/* Fast replacement of a single node without remove/rebalance/add/rebalance */
static inline void idx_rb_link_node(u32 node_idx, u32 parent_idx,
		int new_is_left, struct idx_rb_root *root)
{
	struct idx_rb_node *node = IDX_NODE(root, node_idx);
	node->rb_parent_index = parent_idx;
	node->rb_color = 0;
	node->rb_left_index = IDX_RB_RESERVED_INDEX;
	node->rb_right_index = IDX_RB_RESERVED_INDEX;
	/* link the new node to its parent */
	if (likely(IDX_EXISTS(parent_idx))) {
		if (new_is_left) {
			IDX_NODE(root, parent_idx)->rb_left_index = node_idx;
		} else {
			IDX_NODE(root, parent_idx)->rb_right_index = node_idx;
		}
	} else {
		/* the idx_rb_tree is empty */
		root->idx_rb_node_index = node_idx;
	}
}

static void idx_rb_replace_node(u32 victim_idx, u32 new_idx, struct idx_rb_root *root)
{
	struct idx_rb_node *victim, *new;
	victim = IDX_NODE(root, victim_idx);
	new = IDX_NODE(root, new_idx);
	u32 parent_idx;
	parent_idx = victim->rb_parent_index;

    /* Copy all the info from the victim to the replacement */
    *new = *victim;

    /* Set the surrounding nodes to point to the replacement */
    if (IDX_EXISTS(victim->rb_left_index)) {
		idx_rb_set_parent(root, victim->rb_left_index, new_idx);
	}
    if (IDX_EXISTS(victim->rb_right_index)) {
		idx_rb_set_parent(root, victim->rb_right_index,	new_idx);
	}
    __idx_rb_change_child(victim_idx, new_idx, parent_idx, root);
}

static void idx_rb_insert_color(u32 node_idx, struct idx_rb_root *root)
{
	u32 parent_idx, gparent_idx, tmp_idx;
	parent_idx = idx_rb_red_parent(root, node_idx);
    while (true) {
        /*
         * Loop invariant: node is red
         *
         * If there is a black parent, we are done.
         * Otherwise, take some corrective action as we don't
         * want a red root or two consecutive red nodes.
         */
		if (!IDX_EXISTS(parent_idx)) {
			idx_rb_set_parent_color(root, node_idx, IDX_RB_RESERVED_INDEX,
					IDX_RB_BLACK);
			break;
		} else if (idx_rb_is_black(root, parent_idx)) {
			break;
		}
		gparent_idx = idx_rb_red_parent(root, parent_idx);
		tmp_idx = IDX_NODE(root, gparent_idx)->rb_right_index;
		if (parent_idx != tmp_idx) { /* parent == gparent->rb_left */
			if (IDX_EXISTS(tmp_idx) &&
					idx_rb_is_red(root, tmp_idx)) {
                /*
				 * BLACK: capital letter;
				 * red: lowercase letter;
                 * Case 1 - node's uncle is red (color flips).
                 *
                 *       G            g
                 *      / \          / \
                 *     p   u  -->   P   U
                 *    /            /
                 *   n            n
                 *
                 * However, since g's parent might be red, and
                 * 4) does not allow this, we need to recurse
                 * at g.
                 */
                idx_rb_set_parent_color(root, tmp_idx, gparent_idx,
						IDX_RB_BLACK);
                idx_rb_set_parent_color(root, parent_idx, gparent_idx,
						IDX_RB_BLACK);
                node_idx = gparent_idx;
                parent_idx = idx_rb_parent(root, node_idx);
                idx_rb_set_parent_color(root, node_idx, parent_idx, IDX_RB_RED);
                continue;
			}
			tmp_idx = IDX_NODE(root, parent_idx)->rb_right_index;
            if (node_idx == tmp_idx) {
                /*
				 * Case 2 - node's uncle is black and node is
				 * the parent's right child (left rotate at parent).
                 *
                 *      G             G
                 *     / \           / \
                 *    p   U  -->    n   U
                 *     \           /
                 *      n         p
                 *
                 * This still leaves us in violation of 4), the
                 * continuation into Case 3 will fix that.
                 */
				tmp_idx = IDX_NODE(root, node_idx)->rb_left_index;
				WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_right_index,
						tmp_idx);
				WRITE_ONCE_FOO(IDX_NODE(root, node_idx)->rb_left_index,
						parent_idx);
                if (IDX_EXISTS(tmp_idx)) {
					idx_rb_set_parent_color(root, tmp_idx, parent_idx,
							IDX_RB_BLACK);
				}
				idx_rb_set_parent_color(root, parent_idx, node_idx, IDX_RB_RED);
                //augment_rotate(parent, node); // this is dummy in our case.
                parent_idx = node_idx;
				tmp_idx = IDX_NODE(root, node_idx)->rb_right_index;
            }
            /*
			 * Case 3 - node's uncle is black and node is
			 * the parent's left child (right rotate at gparent).
             *
             *        G           P
             *       / \         / \
             *      p   U  -->  n   g
             *     /                 \
             *    n                   U
             */
			WRITE_ONCE_FOO(IDX_NODE(root, gparent_idx)->rb_left_index,
					tmp_idx); /* == parent->rb_right */
			WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_right_index,
					gparent_idx);
            if (IDX_EXISTS(tmp_idx)) {
                idx_rb_set_parent_color(root, tmp_idx, gparent_idx,
						IDX_RB_BLACK);
			}
            __idx_rb_rotate_set_parents(gparent_idx, parent_idx, root,
					IDX_RB_RED);
            //augment_rotate(gparent, parent);
            break;
		} else {
			tmp_idx = IDX_NODE(root, gparent_idx)->rb_left_index;
			if (IDX_EXISTS(tmp_idx) && idx_rb_is_red(root, tmp_idx)) {
                /* Case 1 - color flips */
                idx_rb_set_parent_color(root, tmp_idx, gparent_idx,
						IDX_RB_BLACK);
                idx_rb_set_parent_color(root, parent_idx, gparent_idx,
						IDX_RB_BLACK);
                node_idx = gparent_idx;
                parent_idx = idx_rb_parent(root, node_idx);
                idx_rb_set_parent_color(root, node_idx, parent_idx, IDX_RB_RED);
                continue;
			}
			tmp_idx = IDX_NODE(root, parent_idx)->rb_left_index;
            if (node_idx == tmp_idx) {
				/* Case 2 - right rotate at parent */
				tmp_idx = IDX_NODE(root, node_idx)->rb_right_index;
				WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_left_index,
						tmp_idx);
				WRITE_ONCE_FOO(IDX_NODE(root, node_idx)->rb_right_index,
						parent_idx);
                if (IDX_EXISTS(tmp_idx)) {
					idx_rb_set_parent_color(root, tmp_idx, parent_idx,
							IDX_RB_BLACK);
				}
				idx_rb_set_parent_color(root, parent_idx, node_idx, IDX_RB_RED);
                //augment_rotate(parent, node); // this is dummy in our case.
                parent_idx = node_idx;
				tmp_idx = IDX_NODE(root, node_idx)->rb_left_index;
            }
			/* Case 3 - left rotate at gparent */
			WRITE_ONCE_FOO(IDX_NODE(root, gparent_idx)->rb_right_index,
					tmp_idx); /* == parent->rb_right */
			WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_left_index,
					gparent_idx);
            if (IDX_EXISTS(tmp_idx)) {
                idx_rb_set_parent_color(root, tmp_idx, gparent_idx,
						IDX_RB_BLACK);
			}
            __idx_rb_rotate_set_parents(gparent_idx, parent_idx, root,
					IDX_RB_RED);
            //augment_rotate(gparent, parent);
            break;
		}
	}
}

static inline u32
__idx_rb_erase_augmented(u32 node_idx, struct idx_rb_root *root)
{
	u32 child_idx, tmp_idx, parent_idx, rebalance_idx, pc_color, pc_parent_idx;
	rebalance_idx = IDX_RB_RESERVED_INDEX;
	child_idx = IDX_NODE(root, node_idx)->rb_right_index;
	tmp_idx = IDX_NODE(root, node_idx)->rb_left_index;
	if (!IDX_EXISTS(tmp_idx)) {
        /*
         * Case 1: node to erase has no more than 1 child (easy!)
         *
         * Note that if there is one child it must be red due to 5)
         * and node must be black due to 4). We adjust colors locally
         * so as to bypass __rb_erase_color() later on.
         */
		pc_color = IDX_NODE(root, node_idx)->rb_color;
		pc_parent_idx = IDX_NODE(root, node_idx)->rb_parent_index;
		parent_idx = pc_parent_idx;
		__idx_rb_change_child(node_idx, child_idx, parent_idx, root);
		if (IDX_EXISTS(child_idx)) {
			IDX_NODE(root, child_idx)->rb_color = pc_color;
			IDX_NODE(root, child_idx)->rb_parent_index = pc_parent_idx;
			rebalance_idx = IDX_RB_RESERVED_INDEX;
		} else {
			rebalance_idx = __idx_color_is_black(pc_color) ?
				parent_idx : IDX_RB_RESERVED_INDEX;
		}
		tmp_idx = parent_idx;
	} else if (!IDX_EXISTS(child_idx)) {
		/* Still case 1, but this time the child is node->rb_left */
		pc_color = IDX_NODE(root, node_idx)->rb_color;
		pc_parent_idx = IDX_NODE(root, node_idx)->rb_parent_index;
		IDX_NODE(root, tmp_idx)->rb_color = pc_color;
		IDX_NODE(root, tmp_idx)->rb_parent_index = pc_parent_idx;
		parent_idx = pc_parent_idx;
		__idx_rb_change_child(node_idx, tmp_idx, parent_idx, root);
		rebalance_idx = IDX_RB_RESERVED_INDEX;
		tmp_idx = parent_idx;
	} else {
		u32 successor_idx, child2_idx;
		successor_idx = child_idx;

		tmp_idx = IDX_NODE(root, child_idx)->rb_left_index;
		if (!IDX_EXISTS(tmp_idx)) {
            /*
             * Case 2: node's successor is its right child
             *
             *    (n)          (s)
             *    / \          / \
             *  (x) (s)  ->  (x) (c)
             *        \
             *        (c)
             */
            parent_idx = successor_idx;
            child2_idx = IDX_NODE(root, successor_idx)->rb_right_index;

            //augment->copy(node, successor);
		} else {
            /*
             * Case 3: node's successor is leftmost under
             * node's right child subtree
             *
             *    (n)          (s)
             *    / \          / \
             *  (x) (y)  ->  (x) (y)
             *      /            /
             *    (p)          (p)
             *    /            /
             *  (s)          (c)
             *    \
             *    (c)
             */
            do {
                parent_idx = successor_idx;
                successor_idx = tmp_idx;
                tmp_idx = IDX_NODE(root, tmp_idx)->rb_left_index;
            } while (IDX_EXISTS(tmp_idx));
            child2_idx = IDX_NODE(root, successor_idx)->rb_right_index;
            WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_left_index, child2_idx);
            WRITE_ONCE_FOO(IDX_NODE(root, successor_idx)->rb_right_index, child_idx);
            idx_rb_set_parent(root, child_idx, successor_idx);

            //augment->copy(node, successor);
            //augment->propagate(parent, successor);
		}
        tmp_idx = IDX_NODE(root, node_idx)->rb_left_index;
        WRITE_ONCE_FOO(IDX_NODE(root, successor_idx)->rb_left_index, tmp_idx);
        idx_rb_set_parent(root, tmp_idx, successor_idx);

        pc_color = IDX_NODE(root, node_idx)->rb_color;
		pc_parent_idx = IDX_NODE(root, node_idx)->rb_parent_index;
        tmp_idx = pc_parent_idx;
        __idx_rb_change_child(node_idx, successor_idx, tmp_idx, root);

        if (IDX_EXISTS(child2_idx)) {
            IDX_NODE(root, successor_idx)->rb_parent_index = pc_parent_idx;
            IDX_NODE(root, successor_idx)->rb_color = pc_color;
            idx_rb_set_parent_color(root, child2_idx, parent_idx, IDX_RB_BLACK);
            rebalance_idx = IDX_RB_RESERVED_INDEX;
        } else {
			u32 pc2_color = IDX_NODE(root, successor_idx)->rb_color;
			u32 pc2_parent_idx = IDX_NODE(root, successor_idx)->rb_parent_index;
            IDX_NODE(root, successor_idx)->rb_parent_index = pc_parent_idx;
            IDX_NODE(root, successor_idx)->rb_color = pc_color;
            rebalance_idx = __idx_color_is_black(pc2_color) ?
				parent_idx : IDX_RB_RESERVED_INDEX;
        }
        tmp_idx = successor_idx;
	}
	//augment->propagate(tmp, NULL);
	return rebalance_idx;
}

static inline void
__idx_rb_erase_color(u32 parent_idx, struct idx_rb_root *root)
{
	u32 node_idx, sibling_idx, tmp1_idx, tmp2_idx;
	node_idx = IDX_RB_RESERVED_INDEX;
	while (true) {
        /*
         * Loop invariants:
         * - node is black (or NULL on first iteration)
         * - node is not the root (parent is not NULL)
         * - All leaf paths going through parent and node have a
         *   black node count that is 1 lower than other leaf paths.
         */
		sibling_idx = IDX_NODE(root, parent_idx)->rb_right_index;
		if (node_idx != sibling_idx) { /* node == parent->rb_left */
			if (idx_rb_is_red(root, sibling_idx)) {
					/*
					 * Case 1 - left rotate at parent
					 *
					 *     P               S
					 *    / \             / \
					 *   N   s    -->    p   Sr
					 *      / \         / \
					 *     Sl  Sr      N   Sl
					 */
                tmp1_idx = IDX_NODE(root, sibling_idx)->rb_left_index;
                WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_right_index,
						tmp1_idx);
                WRITE_ONCE_FOO(IDX_NODE(root, sibling_idx)->rb_left_index,
						parent_idx);
                idx_rb_set_parent_color(root, tmp1_idx, parent_idx,
						IDX_RB_BLACK);
                __idx_rb_rotate_set_parents(parent_idx, sibling_idx, root,
                            IDX_RB_RED);
                //augment_rotate(parent, sibling);
                sibling_idx = tmp1_idx;
			}
			tmp1_idx = IDX_NODE(root, sibling_idx)->rb_right_index;
			if (!IDX_EXISTS(tmp1_idx) || idx_rb_is_black(root, tmp1_idx)) {
				tmp2_idx = IDX_NODE(root, sibling_idx)->rb_left_index;
                if (!IDX_EXISTS(tmp2_idx) || idx_rb_is_black(root, tmp2_idx)) {
                    /*
                     * Case 2 - sibling color flip
                     * (p could be either color here)
                     *
                     *    (p)           (p)
                     *    / \           / \
                     *   N   S    -->  N   s
                     *      / \           / \
                     *     Sl  Sr        Sl  Sr
                     *
                     * This leaves us violating 5) which
                     * can be fixed by flipping p to black
                     * if it was red, or by recursing at p.
                     * p is red when coming from Case 1.
                     */
                    idx_rb_set_parent_color(root, sibling_idx, parent_idx,
                                IDX_RB_RED);
                    if (idx_rb_is_red(root, parent_idx)) {
                        idx_rb_set_black(root, parent_idx);
					} else {
                        node_idx = parent_idx;
                        parent_idx = idx_rb_parent(root, node_idx);
                        if (IDX_EXISTS(parent_idx))
                            continue;
                    }
                    break;
				}
                /*
                 * Case 3 - right rotate at sibling
                 * (p could be either color here)
                 *
                 *   (p)           (p)
                 *   / \           / \
				 *  N   S    -->  N   sl
                 *     / \             \
                 *    sl  Sr            S
                 *                       \
                 *                        Sr
				 *
				 * Note: p might be red, and then both
				 * p and sl are red after rotation(which
				 * breaks property 4). This is fixed in
				 * Case 4 (in __rb_rotate_set_parents()
				 *         which set sl the color of p
				 *         and set p RB_BLACK)
				 *
				 *   (p)            (sl)
				 *   / \            /  \
				 *  N   sl   -->   P    S
				 *       \        /      \
				 *        S      N        Sr
				 *         \
				 *          Sr
                 */
                tmp1_idx = IDX_NODE(root, tmp2_idx)->rb_right_index;
                WRITE_ONCE_FOO(IDX_NODE(root, sibling_idx)->rb_left_index,
						tmp1_idx);
                WRITE_ONCE_FOO(IDX_NODE(root, tmp2_idx)->rb_right_index,
						sibling_idx);
                WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_right_index,
						tmp2_idx);
                if (IDX_EXISTS(tmp1_idx)) {
                    idx_rb_set_parent_color(root, tmp1_idx, sibling_idx,
                                IDX_RB_BLACK);
				}
                //augment_rotate(sibling, tmp2);
                tmp1_idx = sibling_idx;
                sibling_idx = tmp2_idx;
			}
            /*
             * Case 4 - left rotate at parent + color flips
             * (p and sl could be either color here.
             *  After rotation, p becomes black, s acquires
             *  p's color, and sl keeps its color)
             *
             *      (p)             (s)
             *      / \             / \
             *     N   S     -->   P   Sr
             *        / \         / \
             *      (sl) sr      N  (sl)
             */
            tmp2_idx = IDX_NODE(root, sibling_idx)->rb_left_index;
            WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_right_index, tmp2_idx);
            WRITE_ONCE_FOO(IDX_NODE(root, sibling_idx)->rb_left_index, parent_idx);
            idx_rb_set_parent_color(root, tmp1_idx, sibling_idx, IDX_RB_BLACK);
            if (IDX_EXISTS(tmp2_idx)) {
                idx_rb_set_parent(root, tmp2_idx, parent_idx);
			}
            __idx_rb_rotate_set_parents(parent_idx, sibling_idx, root,
                        IDX_RB_BLACK);
            //augment_rotate(parent, sibling);
            break;
		} else {
            sibling_idx = IDX_NODE(root, parent_idx)->rb_left_index;
            if (idx_rb_is_red(root, sibling_idx)) {
                /* Case 1 - right rotate at parent */
                tmp1_idx = IDX_NODE(root, sibling_idx)->rb_right_index;
                WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_left_index,
						tmp1_idx);
                WRITE_ONCE_FOO(IDX_NODE(root, sibling_idx)->rb_right_index,
						parent_idx);
                idx_rb_set_parent_color(root, tmp1_idx, parent_idx,
						IDX_RB_BLACK);
                __idx_rb_rotate_set_parents(parent_idx, sibling_idx, root,
                            IDX_RB_RED);
                //augment_rotate(parent, sibling);
                sibling_idx = tmp1_idx;
            }
			tmp1_idx = IDX_NODE(root, sibling_idx)->rb_left_index;
			if (!IDX_EXISTS(tmp1_idx) || idx_rb_is_black(root, tmp1_idx)) {
				tmp2_idx = IDX_NODE(root, sibling_idx)->rb_right_index;
                if (!IDX_EXISTS(tmp2_idx) || idx_rb_is_black(root, tmp2_idx)) {
					/* Case 2 - sibling color flip */
                    idx_rb_set_parent_color(root, sibling_idx, parent_idx,
                                IDX_RB_RED);
                    if (idx_rb_is_red(root, parent_idx)) {
                        idx_rb_set_black(root, parent_idx);
					} else {
                        node_idx = parent_idx;
                        parent_idx = idx_rb_parent(root, node_idx);
                        if (IDX_EXISTS(parent_idx))
                            continue;
                    }
                    break;
				}
				/* Case 3 - left rotate at sibling */
                tmp1_idx = IDX_NODE(root, tmp2_idx)->rb_left_index;
                WRITE_ONCE_FOO(IDX_NODE(root, sibling_idx)->rb_right_index,
						tmp1_idx);
                WRITE_ONCE_FOO(IDX_NODE(root, tmp2_idx)->rb_left_index,
						sibling_idx);
                WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_left_index,
						tmp2_idx);
                if (IDX_EXISTS(tmp1_idx)) {
                    idx_rb_set_parent_color(root, tmp1_idx, sibling_idx,
                                IDX_RB_BLACK);
				}
                //augment_rotate(sibling, tmp2);
                tmp1_idx = sibling_idx;
                sibling_idx = tmp2_idx;
			}
			/* Case 4 - right rotate at parent + color flips */
            tmp2_idx = IDX_NODE(root, sibling_idx)->rb_right_index;
            WRITE_ONCE_FOO(IDX_NODE(root, parent_idx)->rb_left_index, tmp2_idx);
            WRITE_ONCE_FOO(IDX_NODE(root, sibling_idx)->rb_right_index,
					parent_idx);
            idx_rb_set_parent_color(root, tmp1_idx, sibling_idx, IDX_RB_BLACK);
            if (IDX_EXISTS(tmp2_idx)) {
                idx_rb_set_parent(root, tmp2_idx, parent_idx);
			}
            __idx_rb_rotate_set_parents(parent_idx, sibling_idx, root,
                        IDX_RB_BLACK);
            //augment_rotate(parent, sibling);
            break;
		}
	}
}

static void idx_rb_erase(u32 node_idx, struct idx_rb_root *root)
{
    u32 rebalance_idx;
    rebalance_idx = __idx_rb_erase_augmented(node_idx, root);
    if (IDX_EXISTS(rebalance_idx)) {
        __idx_rb_erase_color(rebalance_idx, root);
	}
}

#define IDX_RB_VERIFY_SUCCESS (1)
#define IDX_RB_VERIFY_FAILED  (!IDX_RB_VERIFY_SUCCESS)
/*
 *  1) A node is either red or black
 */
static int __verify_rule_1(struct idx_rb_root *root)
{
	/* No need to verify */
	return IDX_RB_VERIFY_SUCCESS;
}

/*
 *  2) The root is black
 */
static int __verify_rule_2(struct idx_rb_root *root)
{
	u32 index = root->idx_rb_node_index;
	if (!IDX_EXISTS(index)) {
		return IDX_RB_VERIFY_SUCCESS;
	} else {
		if (idx_rb_is_black(root, index)) {
			return IDX_RB_VERIFY_SUCCESS;
		}
	}
	return IDX_RB_VERIFY_FAILED;
}

/*
 *  3) All leaves (NULL) are black
 */
static int __verify_rule_3(struct idx_rb_root *root)
{
	/* No need to verify */
	return IDX_RB_VERIFY_SUCCESS;
}

static int __verify_rule_4_node(struct idx_rb_root *root, u32 index)
{
	if (!IDX_EXISTS(index)) {
		return IDX_RB_VERIFY_SUCCESS;
	}
	/* inorder */
	int ret = 0, ret_left = 0, ret_right = 0;
	u32 child_idx;
	/* left */
	ret_left = __verify_rule_4_node(root, IDX_NODE(root, index)->rb_left_index);
	/* main */
	ret = IDX_RB_VERIFY_SUCCESS;
	if (idx_rb_is_red(root, index)) {
		child_idx = IDX_NODE(root, index)->rb_left_index;
		if (IDX_EXISTS(child_idx)) {
			if (!idx_rb_is_black(root, child_idx)) {
				ret = IDX_RB_VERIFY_FAILED;
				IDX_RB_print("[Failed] Rule 4: red index=%u\n", index);
			}
		}
		child_idx = IDX_NODE(root, index)->rb_right_index;
		if (IDX_EXISTS(child_idx)) {
			if (!idx_rb_is_black(root, child_idx)) {
				ret = IDX_RB_VERIFY_FAILED;
				IDX_RB_print("[Failed] Rule 4: red index=%u\n", index);
			}
		}
	}
	/* right */
	ret_right = __verify_rule_4_node(root,
			IDX_NODE(root, index)->rb_right_index);
	if (ret_left != IDX_RB_VERIFY_SUCCESS || ret_right != IDX_RB_VERIFY_SUCCESS
			|| ret != IDX_RB_VERIFY_SUCCESS) {
		ret = IDX_RB_VERIFY_FAILED;
	}
	return ret;
}

/*
 *  4) Both children of every red node are black
 */
static int __verify_rule_4(struct idx_rb_root *root)
{
	if (!IDX_EXISTS(root->idx_rb_node_index)) {
		return IDX_RB_VERIFY_SUCCESS;
	}
	return __verify_rule_4_node(root, root->idx_rb_node_index);
}

/* return the number of black nodes */
static unsigned long __verify_rule_5_node(struct idx_rb_root *root, u32 index,
		int *result)
{
	/* post-order to verify at sub-root */
	if (!IDX_EXISTS(index)) {
		/* NIL is black*/
		return 1;
	}
	unsigned long cnt, cnt_left, cnt_right;
	/* left */
	cnt_left = __verify_rule_5_node(root,
			IDX_NODE(root, index)->rb_left_index, result);
	/* right */
	cnt_right = __verify_rule_5_node(root,
			IDX_NODE(root, index)->rb_right_index, result);
	/* main */
	if (cnt_left != cnt_right) {
		IDX_RB_print("[Error] Rule 5 violation, index=%u;"
				"cnt_left=%lu; cnt_right=%lu; root_index=%u.\n", index,
				cnt_left, cnt_right, root->idx_rb_node_index);
		*result = IDX_RB_VERIFY_FAILED;
	}
	cnt = cnt_left;
	if (idx_rb_is_black(root, index)) {
		cnt += 1;
	}
	return cnt;
}

/*
 *  5) Every simple path from root to leaves contains the same number
 *     of black nodes.
 */
static int __verify_rule_5(struct idx_rb_root *root)
{
	if (!IDX_EXISTS(root->idx_rb_node_index)) {
		return IDX_RB_VERIFY_SUCCESS;
	}
	int result = IDX_RB_VERIFY_SUCCESS;
	__verify_rule_5_node(root, root->idx_rb_node_index, &result);
	return result;
}

/* idx_rb verification entry */
static int idx_rb_verify_rules(struct idx_rb_root *root)
{
	int ret = 0;
	if (!IDX_EXISTS(root->idx_rb_node_index)) {
		/* IDX_RB root is empty */
		return IDX_RB_VERIFY_SUCCESS;
	}
	if (__verify_rule_1(root) != IDX_RB_VERIFY_SUCCESS) {
		IDX_RB_print("[Verify] Rule 1 Failed.\n");
		ret++;
	}
	if (__verify_rule_2(root) != IDX_RB_VERIFY_SUCCESS) {
		IDX_RB_print("[Verify] Rule 2 Failed.\n");
		ret++;
	}
	if (__verify_rule_3(root) != IDX_RB_VERIFY_SUCCESS) {
		IDX_RB_print("[Verify] Rule 3 Failed.\n");
		ret++;
	}
	if (__verify_rule_4(root) != IDX_RB_VERIFY_SUCCESS) {
		IDX_RB_print("[Verify] Rule 4 Failed.\n");
		ret++;
	}
	if (__verify_rule_5(root) != IDX_RB_VERIFY_SUCCESS) {
		IDX_RB_print("[Verify] Rule 5 Failed.\n");
		ret++;
	}
	if (ret == 0) {
		return IDX_RB_VERIFY_SUCCESS;
	}
	return IDX_RB_VERIFY_FAILED;
}

#endif
