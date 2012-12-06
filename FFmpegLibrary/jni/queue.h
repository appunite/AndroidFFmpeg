/*
 * queue.h
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

#ifndef QUEUE_H_
#define QUEUE_H_

#include <pthread.h>

typedef struct _Queue Queue;

typedef void * (*queue_fill_func)(void * obj);
typedef void (*queue_free_func)(void * obj, void *elem);

typedef enum {
	QUEUE_CHECK_FUNC_RET_WAIT = -1,
	QUEUE_CHECK_FUNC_RET_TEST = 0,
	QUEUE_CHECK_FUNC_RET_SKIP = 1
} QueueCheckFuncRet;

typedef QueueCheckFuncRet (*QueueCheckFunc)(Queue *queue, void* check_data,
		void *check_ret_data);

Queue *queue_init_with_custom_lock(int size, queue_fill_func fill_func,
		queue_free_func free_func, void *obj, void *free_obj,
		pthread_mutex_t *custom_lock, pthread_cond_t *custom_cond);
void queue_free(Queue *queue, pthread_mutex_t * mutex, pthread_cond_t *cond,
		void *free_obj);

void *queue_push_start_already_locked(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, int *to_write, QueueCheckFunc func,
		void *check_data, void *check_ret_data);
void *queue_push_start(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, int *to_write, QueueCheckFunc func,
		void *check_data, void *check_ret_data);
void queue_push_finish_already_locked(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, int to_write);
void queue_push_finish(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, int to_write);

void *queue_pop_start_already_locked_non_block(Queue *queue);
void *queue_pop_start_already_locked(Queue **queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, QueueCheckFunc func, void *check_data,
		void *check_ret_data);
void *queue_pop_start(Queue **queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond, QueueCheckFunc func, void *check_data,
		void *check_ret_data);
void queue_pop_roll_back_already_locked(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond);
void queue_pop_roll_back(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond);
void queue_pop_finish_already_locked(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond);
void queue_pop_finish(Queue *queue, pthread_mutex_t * mutex,
		pthread_cond_t *cond);

int queue_get_size(Queue *queue);

void queue_wait_for(Queue *queue, int size, pthread_mutex_t * mutex,
		pthread_cond_t *cond);

#endif /* QUEUE_H_ */
