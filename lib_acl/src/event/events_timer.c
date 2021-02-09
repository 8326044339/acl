#include "StdAfx.h"
#ifndef ACL_PREPARE_COMPILE

#include "stdlib/acl_define.h"
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#ifdef ACL_BCB_COMPILER
#pragma hdrstop
#endif

#include "stdlib/acl_mymalloc.h"
#include "stdlib/acl_msg.h"
#include "stdlib/acl_ring.h"
#include "event/acl_events.h"

#endif

#include "events.h"

struct EVENT_TIMERS {
	ACL_HTABLE *table;		/**< ��ϣ�����ڰ���ֵ��ѯ      */
	avl_tree_t  avl;		/**< ���ڰ�ʱ�������ƽ������� */
};

typedef struct TIMER_INFO TIMER_INFO;
typedef struct TIMER_NODE TIMER_NODE;

/* ÿ��Ԫ�ص��ڲ���������Ԫ��������һ��ͬʱ������Ԫ�����������ڵ� */
struct TIMER_INFO {
	TIMER_NODE *node;
	TIMER_INFO *prev;
	TIMER_INFO *next;

	ACL_EVENT_NOTIFY_TIME callback; /* callback function      */
	void *context;                  /* callback context       */
	int   event_type;		/* event type             */
	int   delay;
	int   keep;
	ACL_RING tmp;
};

/* ������ͬ����ʱ��ص�Ԫ�ش��������ڵ��� */
struct TIMER_NODE {
	acl_int64   when;
	avl_node_t  node;
	TIMER_INFO *head;
	TIMER_INFO *tail;
};

/**
 * AVL �õıȽϻص�����
 */
static int avl_cmp_fn(const void *v1, const void *v2)
{
	const struct TIMER_NODE *n1 = (const struct TIMER_NODE*) v1;
	const struct TIMER_NODE *n2 = (const struct TIMER_NODE*) v2;
	acl_int64 ret = n1->when - n2->when;

	if (ret < 0) {
		return -1;
	} else if (ret > 0) {
		return 1;
	} else {
		return 0;
	}
}

void event_timer_init(ACL_EVENT *eventp)
{
	eventp->timers2 = (EVENT_TIMERS*) acl_mymalloc(sizeof(EVENT_TIMERS));
	eventp->timers2->table = acl_htable_create(1024, 0);
	avl_create(&eventp->timers2->avl, avl_cmp_fn, sizeof(TIMER_INFO),
		   offsetof(TIMER_NODE, node));
}

acl_int64 event_timer_when(ACL_EVENT *eventp)
{
	TIMER_NODE *node = avl_first(&eventp->timers2->avl);
	return node ? node->when : -1;
}

/* event_timer_request - (re)set timer */

static void node_link(TIMER_NODE *node, TIMER_INFO *info)
{
	if (node->tail == NULL) {
		info->prev = info->next = NULL;
		node->head = node->tail = info;
	} else {
		node->tail->next = info;
		info->prev = node->tail;
		info->next = NULL;
		node->tail = info;
	}
	info->node = node;
}

/* return 1 if the info's node has been freed*/
static int node_unlink(ACL_EVENT *eventp, TIMER_INFO *info)
{
	TIMER_NODE *node = info->node;

	acl_assert(node);
	if (info->prev) {
		info->prev->next = info->next;
	} else {
		node->head = info->next;
	}
	if (info->next) {
		info->next->prev = info->prev;
	} else {
		node->tail = info->prev;
	}
	info->node = NULL;

	if (node->head == NULL) {
		avl_remove(&eventp->timers2->avl, node);
		acl_myfree(node);
		return 1;
	}
	return 0;
}

acl_int64 event_timer_request(ACL_EVENT *eventp, ACL_EVENT_NOTIFY_TIME callback,
	void *context, acl_int64 delay, int keep)
{
	TIMER_INFO *info;
	TIMER_NODE *node, iter;
	char key[128];

	/* Make sure we schedule this event at the right time. */
	SET_TIME(eventp->present);

	/**
	 * See if they are resetting an existing timer request. If so, take the
	 * request away from the timer queue so that it can be inserted at the
	 * right place.
	 */

	acl_snprintf(key, sizeof(key), "%p.%p", callback, context);
	info = (TIMER_INFO*) acl_htable_find(eventp->timers2->table, key);

	if (info == NULL) {
		/* If not found, schedule a new timer request. */
		info = (TIMER_INFO *) acl_mycalloc(1, sizeof(TIMER_INFO));
		acl_assert(info);
		info->delay      = delay;
		info->keep       = keep;
		info->callback   = callback;
		info->context    = context;
		info->event_type = ACL_EVENT_TIME;
		acl_ring_init(&info->tmp);
		acl_htable_enter(eventp->timers2->table, key, info);
	} else {
		info->delay = delay;
		info->keep  = keep;
		node_unlink(eventp, info);
	}

	iter.when = eventp->present + delay;
	node = (TIMER_NODE*) avl_find(&eventp->timers2->avl, &iter, NULL);
	if (node == NULL) {
		node = (TIMER_NODE*) acl_mycalloc(1, sizeof(TIMER_NODE));
		node->when = iter.when;
		/**
		 * Insert the request at the right place. Timer requests are
		 * kept sorted to reduce lookup overhead in the event loop.
		 */
		avl_add(&eventp->timers2->avl, node);
	}

	node_link(node, info);
	return node->when;
}

/* event_timer_cancel - cancel timer */

acl_int64 event_timer_cancel(ACL_EVENT *eventp,
	ACL_EVENT_NOTIFY_TIME callback, void *context)
{
	acl_int64  time_left = -1;
	char key[128];
	TIMER_INFO *info;
	TIMER_NODE *first, *node;

	/**
	 * See if they are canceling an existing timer request. Do not complain
	 * when the request is not found. It might have been canceled from some
	 * other thread.
	 */

	SET_TIME(eventp->present);

	acl_snprintf(key, sizeof(key), "%p.%p", callback, context);

	info = (TIMER_INFO*) acl_htable_find(eventp->timers2->table, key);
	if (info == NULL) {
		acl_msg_error("%s(%d): not found key=%s",
			__FUNCTION__ , __LINE__, key);
		return -1;
	}
	acl_assert(info->node);
	node = info->node;
	first = avl_first(&eventp->timers2->avl);
	if (first == node) {
		first = AVL_NEXT(&eventp->timers2->avl, first);
		if (node_unlink(eventp, info) == 0) {
			first = node;
		}
	} else {
		node_unlink(eventp, info);
	}

	if (first) {
		time_left = first->when - eventp->present;
		if (time_left < 0) {
			time_left = 0;
		}
	}

	acl_ring_detach(&info->tmp);
	acl_myfree(info);

	return time_left;
}

void event_timer_keep(ACL_EVENT *eventp, ACL_EVENT_NOTIFY_TIME callback,
	void *context, int keep)
{
	TIMER_INFO *info;
	char key[128];

	acl_snprintf(key, sizeof(key), "%p.%p", callback, context);
	info = (TIMER_INFO*) acl_htable_find(eventp->timers2->table, key);
	if (info) {
		info->keep = keep;
	}
}

int  event_timer_ifkeep(ACL_EVENT *eventp, ACL_EVENT_NOTIFY_TIME callback,
	void *context)
{
	TIMER_INFO *info;
	char key[128];

	acl_snprintf(key, sizeof(key), "%p.%p", callback, context);
	info = (TIMER_INFO*) acl_htable_find(eventp->timers2->table, key);
	return info ? info->keep : 0;
}

void event_timer_trigger(ACL_EVENT *eventp)
{
	ACL_RING *ring;
	ACL_EVENT_NOTIFY_TIME timer_fn;
	void *timer_arg;
	TIMER_NODE *iter;
	TIMER_INFO *info;

	/* �����¼������ʱ��� */

	SET_TIME(eventp->present);

	/* ���ȴ���ʱ���е����� */

	iter = avl_first(&eventp->timers2->avl);
	while (iter) {
		if (iter->when > eventp->present) {
			break;
		}
		info = iter->head;
		while (info) {
			acl_ring_prepend(&eventp->timers, &info->tmp);
			info = info->next;
		}
		iter = AVL_NEXT(&eventp->timers2->avl, iter);
	}

#define TMP_TO_INFO(r) \
	((TIMER_INFO *) ((char *) (r) - offsetof(TIMER_INFO, tmp)))

	while ((ring = acl_ring_pop_head(&eventp->timers)) != NULL) {
		info      = TMP_TO_INFO(ring);
		timer_fn  = info->callback;
		timer_arg = info->context;

		if (info->delay > 0 && info->keep) {
			eventp->timer_request(eventp, info->callback,
				info->context, info->delay, info->keep);
		} else {
			node_unlink(eventp, info);
			acl_ring_detach(&info->tmp);
			acl_myfree(info);
		}

		timer_fn(ACL_EVENT_TIME, eventp, timer_arg);
	}
}
