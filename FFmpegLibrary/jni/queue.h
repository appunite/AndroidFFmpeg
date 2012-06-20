/*
 * queue.h
 *
 *  Created on: Jun 11, 2012
 *      Author: Jacek Marchwicki (jacek.marchwicki@gmail.com)
 */

#ifndef QUEUE_H_
#define QUEUE_H_

typedef struct _Queue Queue;

typedef void * (*queue_fill_func)(void * obj);
typedef void (*queue_free_func)(void * obj, void *elem);

Queue *queue_init(int size, queue_fill_func fill_func, queue_free_func free_func, void *obj);
void queue_free(Queue *queue);

void *queue_push_start(Queue *queue, int *to_write);
void queue_push_finish(Queue *queue, int to_write);

void *queue_pop_start(Queue *queue);
void queue_pop_finish(Queue *queue);

int queue_get_size(Queue *queue);

void queue_wait_for(Queue *queue, int size);


#endif /* QUEUE_H_ */
