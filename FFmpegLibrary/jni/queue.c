/*
 * queue.c
 *
 *  Created on: Jun 11, 2012
 *      Author: Jacek Marchwicki (jacek.marchwicki@gmail.com)
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

	pthread_cond_t main_cond;
	pthread_mutex_t main_lock;
	int size;
	void ** tab;
};

int queue_get_next(Queue *queue, int value) {
	return (value + 1) % queue->size;
}

Queue *queue_init(int size, queue_fill_func fill_func, queue_free_func free_func, void * obj) {
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

	pthread_cond_init(&queue->main_cond, NULL);
	pthread_mutex_init(&queue->main_lock, NULL);

	queue->size = size;

	queue->tab = malloc(sizeof(*queue->tab) * size);
	if (queue->tab == NULL)
		goto free_ready;
	memset(queue->tab, 0, sizeof(*queue->tab)*size);
	int i;
	for (i = queue->size - 1; i >= 0; --i) {
		void * elem = fill_func(obj);
		if (elem == NULL)
			goto free_tabs;
		queue->tab[i] = elem;
	}

	goto end;
free_tabs:
	for (i = queue->size -1; i>=0; --i) {
		void *elem = queue->tab[i];
		if (elem == NULL)
			continue;
		queue->free_func(queue->free_func_obj, elem);
	}

free_tab:
	free(queue->tab);

free_ready:
	free(queue->ready);

free_queue:
	free(queue);
	queue = NULL;

end:
	return queue;
}

void queue_free(Queue *queue) {
	int i;
	for (i = queue->size -1; i>=0; --i) {
		void *elem = queue->tab[i];
		queue->free_func(queue->free_func_obj, elem);
	}

	free(queue->tab);

	free(queue->ready);

	free(queue);
}

void *queue_push_start(Queue *queue, int *to_write) {
	pthread_mutex_lock(&queue->main_lock);
	int next_next_to_write;
	while (1) {
		next_next_to_write = queue_get_next(queue, queue->next_to_write);
		if (next_next_to_write != queue->next_to_read) {
			break;
		}
		pthread_cond_wait(&queue->main_cond, &queue->main_lock);
	}
	*to_write = queue->next_to_write;
	queue->ready[*to_write] = FALSE;

	queue->next_to_write = next_next_to_write;

	pthread_cond_signal(&queue->main_cond);
	pthread_mutex_unlock(&queue->main_lock);

	return queue->tab[*to_write];
}

void queue_push_finish(Queue *queue, int to_write) {
	pthread_mutex_lock(&queue->main_lock);
	queue->ready[to_write] = TRUE;

	pthread_cond_signal(&queue->main_cond);
	pthread_mutex_unlock(&queue->main_lock);
}

void *queue_pop_start(Queue *queue) {
	pthread_mutex_lock(&queue->main_lock);
	assert(!queue->in_read);
	int to_read;
	while (1) {
		if (queue->next_to_read != queue->next_to_write
				&& queue->ready[queue->next_to_read])
			break;
		pthread_cond_wait(&queue->main_cond, &queue->main_lock);
	}
	to_read = queue->next_to_read;
	queue->in_read = TRUE;

	pthread_mutex_unlock(&queue->main_lock);

	return queue->tab[to_read];
}

void queue_pop_finish(Queue *queue) {
	pthread_mutex_lock(&queue->main_lock);
	assert(queue->in_read);
	queue->in_read = FALSE;
	queue->next_to_read = queue_get_next(queue, queue->next_to_read);

	pthread_cond_signal(&queue->main_cond);
	pthread_mutex_unlock(&queue->main_lock);
}

int queue_get_size(Queue *queue) {
	return queue->size;
}

void queue_wait_for(Queue *queue, int size) {
	assert(queue->size >= size);

	pthread_mutex_lock(&queue->main_lock);
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

		pthread_cond_wait(&queue->main_cond, &queue->main_lock);
	}
	pthread_mutex_unlock(&queue->main_lock);
}

