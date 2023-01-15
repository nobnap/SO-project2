#ifndef __BOX_H__
#define __BOX_H__

#include <stdint.h>
#include <pthread.h>

struct box {
	char box_name[32];
	uint64_t n_publishers;
	uint64_t n_subscribers;
	uint64_t box_size; //maybe remove this?
	pthread_mutex_t box_lock;
	pthread_cond_t box_condvar;
	struct box* next;
};

void init_box(struct box* box, const char* box_name);
void destroy_box_list(struct box* node);
void add_box_to_list(struct box* head_box, struct box* new_box);
void remove_box_from_list(struct box* head_box, const char* box_name);
struct box* lookup_box_in_list(struct box* head_box, const char* box_name);

#endif