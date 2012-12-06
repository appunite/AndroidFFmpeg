/*
 * queue.c
 * Copyright (c) 2012 Jacek Marchwicki
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

#include "queue.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#define FALSE 0
#define TRUE (!(FALSE))

struct _Queue {
	int next_to_write;
	int next_to_read;
	int *ready;

	int in_read;

	queue_free_func free_func;

	int is_custom_lock;
	int size;
	void ** tab;
};

int queue_get_next(Queue *queue, int value) {
	return (value + 1) % queue->size;
}

Queue *queue_init_with_custom_lock(int size, queue_fill_func fill_func,
		queue_free_func free_func, void *obj, void *free_obj, pthread_mutex_t *custom_lock,
		pthread_cond_t *custom_cond) {
	Queue *queue = malloc(sizeof(Queue));
	if (queue == NULL)
		return NULL;

	queue->next_to_write = 0;
	queue->next_to_read = 0;
	queue->ready = malloc(sizeof(*queue->ready) * size);
	if (queue->ready == NULL)
		goto free_queue;

	queue->in_read = FALSE;

	queue->free_func = free_func;

	queue->is_custom_lock = TRUE;

	queue->size = size;

	queue->tab = malloc(sizeof(*queue->tab) * size);
	if (queue->tab == NULL)
		goto free_ready;
	memset(queue->tab, 0, sizeof(*queue->tab) * size);
	int i;
	for (i = queue->size - 1; i >= 0; --i) {
		void * elem = fill_func(obj);
		if (elem == NULL)
			goto free_tabs;
		queue->tab[i] = elem;
	}

	goto end;
	free_tabs: for (i = queue->size - 1; i >= 0; --i) {
		void *elem = queue->tab[i];
		if (elem == NULL)
			continue;
		queue->free_func(free_obj, elem);
	}

	free_tab: free(queue->tab);

	free_ready: free(queue->ready);

	free_queue: free(queue);
	queue = NULL;

	end: return queue;
}

void queue_free(Queue *queue, pthread_mutex_t * mutex, pthread_cond_t *cond, void *free_obj) {
	pthread_mutex_lock(mutex);
	while (queue->in_read)
		pthread_cond_wait(cond, mutex);

	int i;
	for (i = queue->size - 1; i >= 0; --i) {
		void *elem = queue->tab[i];
		queue->free_func(free_obj, elem);
	}
	pthread_mutex_unlock(mutex);

	free(queue->tab);

	free(queue->ready);

	free(queue);
}

void *queue_push_start_already_locked(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, int *to_write, QueueCheckFunc func,
		void *check_data, void *check_ret_data) {
	int next_next_to_write;
	while (1) {
		if (func == NULL)
			goto test;
		QueueCheckFuncRet check = func(queue, check_data, check_ret_data);
		if (check == QUEUE_CHECK_FUNC_RET_SKIP)
			return NULL;
		else if (check == QUEUE_CHECK_FUNC_RET_WAIT)
			goto wait;
		else if (check == QUEUE_CHECK_FUNC_RET_TEST)
			goto test;
		else
			assert(FALSE);

		test: next_next_to_write = queue_get_next(queue, queue->next_to_write);
		if (next_next_to_write != queue->next_to_read) {
			break;
		}

		wait: pthread_cond_wait(cond, mutex);
	}
	*to_write = queue->next_to_write;
	queue->ready[*to_write] = FALSE;

	queue->next_to_write = next_next_to_write;

	pthread_cond_broadcast(cond);

	end: return queue->tab[*to_write];
}

void *queue_push_start(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, int *to_write, QueueCheckFunc func,
		void *check_data, void *check_ret_data) {
	void *ret;
	pthread_mutex_lock(mutex);
	ret = queue_push_start_already_locked(queue, mutex, cond, to_write, func,
			check_data, check_ret_data);
	pthread_mutex_unlock(mutex);
	return ret;
}

void queue_push_finish_already_locked(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, int to_write) {
	queue->ready[to_write] = TRUE;
	pthread_cond_broadcast(cond);
}

void queue_push_finish(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, int to_write) {
	pthread_mutex_lock(mutex);
	queue_push_finish_already_locked(queue, mutex, cond, to_write);
	pthread_mutex_unlock(mutex);
}

void *queue_pop_start_already_locked_non_block(Queue *queue) {
	assert(!queue->in_read);
	int to_read = queue->next_to_read;
	if (to_read == queue->next_to_write)
		return NULL;
	if (!queue->ready[to_read])
		return NULL;

	queue->in_read = TRUE;
	return queue->tab[to_read];
}

void *queue_pop_start_already_locked(Queue **queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, QueueCheckFunc func, void *check_data,
		void *check_ret_data) {
	int to_read;
	Queue *q;
	while (1) {
		if (func == NULL)
			goto test;
		QueueCheckFuncRet check = func(*queue, check_data, check_ret_data);
		if (check == QUEUE_CHECK_FUNC_RET_SKIP)
			goto skip;
		else if (check == QUEUE_CHECK_FUNC_RET_WAIT)
			goto wait;
		else if (check == QUEUE_CHECK_FUNC_RET_TEST)
			goto test;
		else
			assert(FALSE);
		test:
		q = *queue;
		assert(!q->in_read);
		if (q->next_to_read != q->next_to_write
				&& q->ready[q->next_to_read])
			break;
		wait: pthread_cond_wait(cond, mutex);
	}
	q=*queue;
	to_read = q->next_to_read;
	q->in_read = TRUE;

	end:

	return q->tab[to_read];

	skip: return NULL;
}

void *queue_pop_start(Queue **queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, QueueCheckFunc func, void *check_data,
		void *check_ret_data) {
	void *ret;
	pthread_mutex_lock(mutex);
	ret = queue_pop_start_already_locked(queue, mutex, cond, func, check_data,
			check_ret_data);
	pthread_mutex_unlock(mutex);
	return ret;
}

void queue_pop_roll_back_already_locked(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond) {
	assert(queue->in_read);
	queue->in_read = FALSE;

	pthread_cond_broadcast(cond);
}

void queue_pop_roll_back(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond) {
	pthread_mutex_lock(mutex);
	queue_pop_roll_back_already_locked(queue, mutex, cond);
	pthread_mutex_unlock(mutex);
}

void queue_pop_finish_already_locked(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond) {
	assert(queue->in_read);
	queue->in_read = FALSE;
	queue->next_to_read = queue_get_next(queue, queue->next_to_read);

	pthread_cond_broadcast(cond);
}

void queue_pop_finish(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond) {
	pthread_mutex_lock(mutex);
	queue_pop_finish_already_locked(queue, mutex, cond);
	pthread_mutex_unlock(mutex);
}

int queue_get_size(Queue *queue) {
	return queue->size;
}

void queue_wait_for(Queue *queue, int size, pthread_mutex_t * mutex,
		pthread_cond_t *cond) {
	assert(queue->size >= size);

	pthread_mutex_lock(mutex);
	while (1) {
		int next = queue->next_to_read;
		int i;
		int all_ok = TRUE;
		for (i = 0; i < size; ++i) {
			if (next == queue->next_to_write
					|| !queue->ready[queue->next_to_read]) {
				all_ok = FALSE;
				break;
			}

			next = queue_get_next(queue, next);
		}

		if (all_ok)
			break;

		pthread_cond_wait(cond, mutex);
	}
	pthread_mutex_unlock(mutex);
}

