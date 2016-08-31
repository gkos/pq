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
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Needed to get struct timespec with -pedantic -ansi */
#define _XOPEN_SOURCE 600

struct pqn { /* pthread queue node */
	void *data;
	struct pqn *next;
};

struct pq_head {
	struct pqn *first;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

typedef struct pqn pqn;
typedef struct pq_head pq_head;
#define PQ_HEAD_INIT (pq_head){NULL,PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER}
extern inline void pq_head_init(pq_head *h)
{
	h->first = NULL;
	pthread_mutex_init(&h->lock, NULL);
	pthread_cond_init(&h->cond, NULL);
}

extern inline int pthread_cond_timedwait_ms(pthread_cond_t *cond, pthread_mutex_t *lock, unsigned timeout)
{
        int status;
        struct timespec ts;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += timeout / 1000;
        ts.tv_nsec += (timeout % 1000 * 1000000);
        ts.tv_sec += ts.tv_nsec / 1000000000;
        ts.tv_nsec %= 1000000000;

        status = pthread_cond_timedwait(cond, lock, &ts);

        return status;
}

extern inline pqn* pqn_new(void *p)
{
	pqn *n = calloc(sizeof(*n), 1);
	assert(n);
	n->data = p;
	return n;
}

extern inline bool pq_isempty(pq_head *h)
{
	return h->first == NULL;
}

extern inline int pq_put_head(pq_head *h, pqn *n)
{
	pthread_mutex_lock(&h->lock);
	n->next = h->first;
	h->first = n;
	pthread_mutex_unlock(&h->lock);
	pthread_cond_signal(&h->cond);
	return 0;
}

/* NOTE THAT THIS "RETURNS" WITH LOCK HELD
 * THE CALLER *MUST* RELEASE THE LOCK. 
 */
#define wait_boilerplate(predicate, cond, lock, tout_ms)\
	({\
		int status;\
		pthread_mutex_lock(lock);\
		while (!(predicate) && status == 0) {\
			if ((tout_ms) > 0) {\
				status = pthread_cond_timedwait_ms(cond, lock, tout_ms);\
			} else {\
				status = pthread_cond_wait(cond, lock);\
			}\
		}\
		status;\
	})

/* lbo stands for last but one */
#define find_lbo(head)\
	({\
		assert((head)->first->next);\
		pqn *lbo;\
		for (lbo = (head)->first; lbo->next->next; lbo = lbo->next)\
			/* empty body */;\
		lbo;\
	})

extern inline pqn *pq_get_tail(pq_head *h, unsigned timeout)
{
	pqn *retptr = NULL;
	int status = wait_boilerplate(!pq_isempty(h), &h->cond, &h->lock, timeout);
	/* h->lock taken by wait_bolierplate */
	if (status == ETIMEDOUT)
		goto out;
	if (!h->first->next) { /* first is the last, one elem/ list */
		retptr = h->first;
		h->first = NULL;
	} else { /* our list has two or more elements */
		pqn *lbo = find_lbo(h);
		retptr = lbo->next;
		lbo->next = NULL;
	}
out:
	pthread_mutex_unlock(&h->lock);
	return retptr;
}

#ifdef __cplusplus
}
#endif

#endif