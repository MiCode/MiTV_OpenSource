/*
 *  kernel/kdebugd/kdbg_util.c
 *
 *  Advance oprofile related functions
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2010.03.05  Created by gaurav.j
 *
 */

#include <kdebugd.h>
#include "kdbg_util.h"

/*
 *  Link list  sorting in ascending order
 */
void aop_list_sort(struct list_head *head, aop_cmp cmp)
{
	struct list_head *p, *q, *e, *list, *tail, *oldhead;
	int insize, nmerges, psize, qsize, i;

	list = head->next;
	list_del(head);
	insize = 1;
	for (;;) {
		p = oldhead = list;
		list = tail = NULL;
		nmerges = 0;

		while (p) {
			nmerges++;
			q = p;
			psize = 0;
			for (i = 0; i < insize; i++) {
				psize++;
				q = q->next == oldhead ? NULL : q->next;
				if (!q)
					break;
			}

			qsize = insize;
			while (psize > 0 || (qsize > 0 && q)) {
				if (!psize) {
					e = q;
					q = q->next;
					qsize--;
					if (q == oldhead)
						q = NULL;
				} else if (!qsize || !q) {
					e = p;
					if (!p) {
						PRINT_KD
						    ("%s: ERROR: p is NULL [1]\n",
						     __FUNCTION__);
						BUG_ON(!p);	/* catch it */
						break;	/* unreachable point */
					}
					p = p->next;
					psize--;
					if (p == oldhead)
						p = NULL;
				} else if (cmp(p, q) <= 0) {
					e = p;
					if (!p) {
						PRINT_KD
						    ("%s: ERROR: p is NULL [2]\n",
						     __FUNCTION__);
						BUG_ON(!p);	/* catch it */
						break;	/* unreachable point */
					}
					p = p->next;
					psize--;
					if (p == oldhead)
						p = NULL;
				} else {
					e = q;
					q = q->next;
					qsize--;
					if (q == oldhead)
						q = NULL;
				}
				if (tail)
					tail->next = e;
				else
					list = e;
				e->prev = tail;
				tail = e;
			}
			p = q;
		}

		tail->next = list;
		list->prev = tail;

		if (nmerges <= 1)
			break;

		insize *= 2;
	}

	head->next = list;
	head->prev = list->prev;
	list->prev->next = head;
	list->prev = head;
}
