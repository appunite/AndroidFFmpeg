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
	void * free_func_obj;

	int is_custom_lock;
	pthread_cond_t *main_cond;
	pthread_mutex_t *main_lock;
	int size;
	void ** tab;
};

int queue_get_next(Queue *queue, int value) {
	return (value + 1) % queue->size;
}

Queue *queue_init_with_custom_lock(int size, queue_fill_func fill_func,
		queue_free_func free_func, void *obj, pthread_mutex_t *custom_lock,
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
	queue->free_func_obj = obj;

	queue->is_custom_lock = TRUE;
	queue->main_cond = custom_cond;
	queue->main_lock = custom_lock;

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
		queue->free_func(queue->free_func_obj, elem);
	}

	free_tab: free(queue->tab);

	free_ready: free(queue->ready);

	free_queue: free(queue);
	queue = NULL;

	end: return queue;
}

Queue *queue_init(int size, queue_fill_func fill_func,
		queue_free_func free_func, void * obj) {
	Queue *queue = NULL;

	pthread_mutex_t *main_lock = malloc(sizeof(*main_lock));
	if (!main_lock)
		goto end;

	pthread_cond_t *main_cond = malloc(sizeof(*main_cond));
	if (!main_cond)
		goto free_main_lock;

	queue = queue_init_with_custom_lock(size, fill_func, free_func, obj,
			main_lock, main_cond);
	if (!queue)
		goto free_main_cond;

	queue->is_custom_lock = FALSE;

	goto end;

	free_main_cond: free(main_cond);

	free_main_lock: free(main_lock);

	end: return queue;
}

void queue_free(Queue *queue) {
	int i;
	for (i = queue->size - 1; i >= 0; --i) {
		void *elem = queue->tab[i];
		queue->free_func(queue->free_func_obj, elem);
	}

	free(queue->tab);

	free(queue->ready);

	free(queue);
}

void *queue_push_start_already_locked(Queue *queue, int *to_write, QueueCheckFunc func,
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

		test:
		next_next_to_write = queue_get_next(queue, queue->next_to_write);
		if (next_next_to_write != queue->next_to_read) {
			break;
		}

		wait:
		pthread_cond_wait(queue->main_cond, queue->main_lock);
	}
	*to_write = queue->next_to_write;
	queue->ready[*to_write] = FALSE;

	queue->next_to_write = next_next_to_write;

	pthread_cond_broadcast(queue->main_cond);

	end:
	return queue->tab[*to_write];
}

void *queue_push_start(Queue *queue, int *to_write, QueueCheckFunc func,
		void *check_data, void *check_ret_data) {
	void *ret;
	pthread_mutex_lock(queue->main_lock);
	ret = queue_push_start_already_locked(queue, to_write, func, check_data, check_ret_data);
	pthread_mutex_unlock(queue->main_lock);
	return ret;
}

void queue_push_finish(Queue *queue, int to_write) {
	pthread_mutex_lock(queue->main_lock);
	queue->ready[to_write] = TRUE;

	pthread_cond_broadcast(queue->main_cond);
	pthread_mutex_unlock(queue->main_lock);
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

void *queue_pop_start_already_locked(Queue *queue, QueueCheckFunc func, void *check_data,
		void *check_ret_data) {
	assert(!queue->in_read);
	int to_read;
	while (1) {
		if (func == NULL)
			goto test;
		QueueCheckFuncRet check = func(queue, check_data, check_ret_data);
		if (check == QUEUE_CHECK_FUNC_RET_SKIP)
			goto skip;
		else if (check == QUEUE_CHECK_FUNC_RET_WAIT)
			goto wait;
		else if (check == QUEUE_CHECK_FUNC_RET_TEST)
			goto test;
		else
			assert(FALSE);
		test:
		if (queue->next_to_read != queue->next_to_write
				&& queue->ready[queue->next_to_read])
			break;
		wait:
		pthread_cond_wait(queue->main_cond, queue->main_lock);
	}
	to_read = queue->next_to_read;
	queue->in_read = TRUE;

	end:

	return queue->tab[to_read];

	skip:
	return NULL;
}

void *queue_pop_start(Queue *queue, QueueCheckFunc func, void *check_data,
		void *check_ret_data) {
	void *ret;
	pthread_mutex_lock(queue->main_lock);
	ret = queue_pop_start_already_locked(queue, func, check_data, check_ret_data);
	pthread_mutex_unlock(queue->main_lock);
	return ret;
}

void queue_pop_roll_back_already_locked(Queue *queue) {
	assert(queue->in_read);
	queue->in_read = FALSE;

	pthread_cond_broadcast(queue->main_cond);
}

void queue_pop_roll_back(Queue *queue) {
	pthread_mutex_lock(queue->main_lock);
	queue_pop_roll_back_already_locked(queue);
	pthread_mutex_unlock(queue->main_lock);
}

void queue_pop_finish_already_locked(Queue *queue) {
	assert(queue->in_read);
	queue->in_read = FALSE;
	queue->next_to_read = queue_get_next(queue, queue->next_to_read);

	pthread_cond_broadcast(queue->main_cond);
}

void queue_pop_finish(Queue *queue) {
	pthread_mutex_lock(queue->main_lock);
	queue_pop_finish_already_locked(queue);
	pthread_mutex_unlock(queue->main_lock);
}

int queue_get_size(Queue *queue) {
	return queue->size;
}
//void queue_interrupt(Queue *queue, QueueInterruptFunc func, void *data) {
//	pthread_mutex_lock(&queue->main_lock);
//	func(queue, data);
//	pthread_cond_broadcast(&queue->main_cond);
//	pthread_mutex_unlock(&queue->main_lock);
//}

void queue_wait_for(Queue *queue, int size) {
	assert(queue->size >= size);

	pthread_mutex_lock(queue->main_lock);
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

		pthread_cond_wait(queue->main_cond, queue->main_lock);
	}
	pthread_mutex_unlock(queue->main_lock);
}

