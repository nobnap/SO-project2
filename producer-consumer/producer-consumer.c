#include "producer-consumer.h"
#include <stdlib.h>

// pcq_create: create a queue, with a given (fixed) capacity
//
// Memory: the queue pointer must be previously allocated
// (either on the stack or the heap)
int pcq_create(pc_queue_t *queue, size_t capacity) {
	// FIXME: do we need to allocate space for buffer
	queue->pcq_buffer = (void **)malloc(capacity * sizeof(void *));
	queue->pcq_capacity = capacity;
	pthread_mutex_init(&queue->pcq_current_size_lock, NULL);
	queue->pcq_current_size = 0;
	pthread_mutex_init(&queue->pcq_head_lock, NULL);
	queue->pcq_head = 0;
	pthread_mutex_init(&queue->pcq_tail_lock, NULL);
	queue->pcq_tail = 0;
	pthread_mutex_init(&queue->pcq_pusher_condvar_lock, NULL);
	pthread_cond_init(&queue->pcq_pusher_condvar, NULL);
	pthread_mutex_init(&queue->pcq_popper_condvar_lock, NULL);
	pthread_cond_init(&queue->pcq_popper_condvar, NULL);
	return 0;
}

// pcq_destroy: releases the internal resources of the queue
//
// Memory: does not free the queue pointer itself
int pcq_destroy(pc_queue_t *queue) {
	free(queue->pcq_buffer); // TODO: check if this is all there is to it
	pthread_mutex_destroy(&queue->pcq_current_size_lock);
	pthread_mutex_destroy(&queue->pcq_head_lock);
	pthread_mutex_destroy(&queue->pcq_tail_lock);
	pthread_mutex_destroy(&queue->pcq_pusher_condvar_lock);
	pthread_cond_destroy(&queue->pcq_pusher_condvar);
	pthread_mutex_destroy(&queue->pcq_popper_condvar_lock);
	pthread_cond_destroy(&queue->pcq_popper_condvar);
	return 0;
}

// pcq_enqueue: insert a new element at the front of the queue
//
// If the queue is full, sleep until the queue has space
int pcq_enqueue(pc_queue_t *queue, void *elem) {
	pthread_mutex_lock(&queue->pcq_current_size_lock);
	while (queue->pcq_current_size == queue->pcq_capacity) {
		pthread_mutex_unlock(&queue->pcq_current_size_lock);
		pthread_cond_wait(&queue->pcq_pusher_condvar,
						  &queue->pcq_pusher_condvar_lock);
	}
	pthread_mutex_unlock(&queue->pcq_current_size_lock);

	pthread_mutex_lock(&queue->pcq_current_size_lock);
	queue->pcq_current_size++;
	pthread_cond_signal(&queue->pcq_popper_condvar);
	pthread_mutex_unlock(&queue->pcq_current_size_lock);

	pthread_mutex_lock(&queue->pcq_head_lock);
	queue->pcq_buffer[queue->pcq_head] = elem;
	if (queue->pcq_head + 1 == queue->pcq_capacity)
		queue->pcq_head = 0;
	else
		queue->pcq_head++;
	pthread_mutex_unlock(&queue->pcq_head_lock);

	return 0;
}

// pcq_dequeue: remove an element from the back of the queue
//
// If the queue is empty, sleep until the queue has an element
void *pcq_dequeue(pc_queue_t *queue) {
	pthread_mutex_lock(&queue->pcq_current_size_lock);
	while (queue->pcq_current_size == 0) {
		pthread_mutex_unlock(&queue->pcq_current_size_lock);
		pthread_cond_wait(&queue->pcq_popper_condvar,
						  &queue->pcq_popper_condvar_lock);
	}
	pthread_mutex_unlock(&queue->pcq_current_size_lock);

	pthread_mutex_lock(&queue->pcq_current_size_lock);
	queue->pcq_current_size--;
	pthread_cond_signal(&queue->pcq_pusher_condvar);
	pthread_mutex_unlock(&queue->pcq_current_size_lock);

	pthread_mutex_lock(&queue->pcq_tail_lock);
	void *elem = queue->pcq_buffer[queue->pcq_tail];
	if (queue->pcq_tail + 1 == queue->pcq_capacity)
		queue->pcq_tail = 0;
	else
		queue->pcq_tail++;
	pthread_mutex_unlock(&queue->pcq_head_lock);

	return elem;
}