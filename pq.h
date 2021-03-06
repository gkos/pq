/*
 * Copyright 2016 Daniel Hilst Selli
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifndef _GKOS_PTHREAD_QUEUE_H
#define _GKOS_PTHREAD_QUEUE_H

#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pqn { /* pthread queue node */
	long data;
	struct pqn *next, *prev;
};

struct pq_head {
	struct pqn *first, *last;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool terminate;
	size_t size;
};

typedef struct pqn pqn;
typedef struct pq_head pq_head;

#define PQ_HEAD_INIT (pq_head){NULL,NULL,PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,false,0}
static inline void pq_head_init(pq_head *h)
{
	h->first = NULL;
	pthread_mutex_init(&h->lock, NULL);
	pthread_cond_init(&h->cond, NULL);
}

static inline int __pthread_cond_timedwait_ms(pthread_cond_t *cond, pthread_mutex_t *lock, unsigned timeout)
{
        int status;
        struct timespec ts;

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout / 1000;
        ts.tv_nsec += (timeout % 1000 * 1000000);
        ts.tv_sec += ts.tv_nsec / 1000000000;
        ts.tv_nsec %= 1000000000;

        status = pthread_cond_timedwait(cond, lock, &ts);

        return status;
}

static inline pqn* pqn_new(void *p)
{
	pqn *n = calloc(sizeof(*n), 1);
	assert(n);
	n->data = (long)p;
	return n;
}

static inline void *pqn_getdata(pqn *n)
{
	return (void *)n->data;
}

static inline void pqn_setdata(pqn *n, void *data)
{
	n->data = (long)data;
}

static inline bool pq_isempty(pq_head *h)
{
	return h->first == NULL;
}
#define pqn_unlink(n) (n->next = n->prev = NULL)

static inline int pq_put_head(pq_head *h, pqn *n)
{
	pthread_mutex_lock(&h->lock);
	pqn_unlink(n); /* I think I'm paranoid */
	if (pq_isempty(h)) {
		h->first = h->last = n;
	} else {
		n->next = h->first;
		h->first->prev = n;
		h->first = n;
	}
	h->size++;
	pthread_cond_signal(&h->cond);
	pthread_mutex_unlock(&h->lock);
	return 0;
}

#define wait_boilerplate(predicate, cond, lock, tout_ms)\
	({\
		int status = 0;\
		while (!(predicate) && status == 0) {\
			if ((tout_ms) > 0) {\
				status = __pthread_cond_timedwait_ms(cond, lock, tout_ms);\
			} else {\
				status = pthread_cond_wait(cond, lock);\
			}\
		}\
		status;\
	})

static inline pqn *pq_get_tail(pq_head *h, unsigned timeout)
{
	pqn *retptr = NULL;
	pthread_mutex_lock(&h->lock);
	int status = wait_boilerplate(!pq_isempty(h), &h->cond, &h->lock, timeout);
	if (status) {
		goto out;
	}
	assert(!pq_isempty(h));
	if (h->first == h->last) {
		retptr = h->first;
		h->first = h->last = NULL;
	} else {
		retptr = h->last;
		h->last = h->last->prev;
	}
	pqn_unlink(retptr);
	h->size--;
out:
	pthread_mutex_unlock(&h->lock);
	return retptr;
}

static inline void pq_terminate(pq_head *h)
{
	h->terminate = true;
	pthread_cond_broadcast(&h->cond);
	h->size = 0;
}

static inline bool pq_isterminated(pq_head *h)
{
	return h->terminate;
}

typedef bool (*pq_foreach_cb)(pqn *node, void *arg);
static inline void pq_foreach(pq_head *h, pq_foreach_cb cb, void *arg)
{
	bool result;
	pqn *pqnp;

	pthread_mutex_lock(&h->lock);
	for (pqnp = h->first; pqnp; pqnp = pqnp->next) {
		pthread_mutex_unlock(&h->lock);
		result = cb(pqnp, arg);
		if (!result)
			break;
		pthread_mutex_lock(&h->lock);
	}
	pthread_mutex_unlock(&h->lock);
}

static inline void pq_foreach_freeze(pq_head *h, pq_foreach_cb cb, void *arg)
{
	pqn *pqnp;

	pthread_mutex_lock(&h->lock);
	for (pqnp = h->first; pqnp; pqnp = pqnp->next) {
		if (!cb(pqnp, arg))
			break;
	}
	pthread_mutex_unlock(&h->lock);
}

static inline size_t pq_size(pq_head *h)
{
	return h->size;
}

#ifdef __cplusplus
}
#endif

#endif
