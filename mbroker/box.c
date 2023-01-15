#include "box.h"
#include <stdlib.h>
#include <string.h>

void init_box(struct box* box, const char* box_name) {
	strcpy(box->box_name, box_name);
	box->n_publishers = 0;
	box->n_subscribers = 0;
	box->box_size = 0;
	pthread_mutex_init(&box->box_lock, NULL);
	pthread_cond_init(&box->box_condvar, NULL);
	box->next = NULL;
}

void destroy_box_list(struct box* node) {
	if (node != NULL) {
		pthread_mutex_destroy(&node->box_lock);
		pthread_cond_destroy(&node->box_condvar);
		destroy_box_list(node->next);
		free(node);
	}
}

struct box* lookup_box_in_list(struct box* head_box, const char* box_name) {
	if (head_box == NULL) {
		return NULL;
	}
	if (!strcmp(box_name, head_box->box_name)) {
		return head_box;
	} else {
		return lookup_box_in_list(head_box->next, box_name);
	}

}
