#include "logging.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include "producer-consumer.h"
#include "operations.h"
#include <signal.h>
#include "box.h"

#define BUFFER_SIZE 128
#define MESSAGE_SIZE 1024
#define MAX_BOX_AMOUNT 128

#define CREATE_BOX_ANSWER_CODE 4
#define REMOVE_BOX_ANSWER_CODE 6
#define LIST_BOX_ANSWER_CODE 8
#define SUBSCRIBER_MESSAGE_CODE 10

// Global box head struct: used as the start of a global box linked list.
struct box* head;

// Global flag variable: used to exit out of the main thread loop when a signal is received.
int flag = 0;

// Global counting variable: used to assign unique IDs to box objects
static int box_count = 0;

static pthread_mutex_t box_list_lock;

static void sighandler() {
	exit(EXIT_SUCCESS);
}

int send_message(int pipenum, char const *box_message) {
	struct message msg = message_init(SUBSCRIBER_MESSAGE_CODE, box_message);
	ssize_t n = write(pipenum, &msg, sizeof(struct message));
	if (n == -1) {
		return -1;
	}
	return 0;
}

int send_answer(const char *client_named_pipe_path, struct box_answer answer) {
	int pipenum = open(client_named_pipe_path, O_WRONLY);
	if (pipenum == -1) {
		return -1; // failed to open pipe
	}

	ssize_t n = write(pipenum, &answer, sizeof(struct box_answer));
	if (n == -1) {
		close(pipenum);
		return -1;
	}

	close(pipenum);
	return 0;
}

int handle_publisher(const char *client_named_pipe_path, const char *box_name) {
	struct box* box = lookup_box_in_list(head, box_name);
	if (box == NULL) {
		return -1; //TODO: implement worker thread response to failed handling
	}

	int pub_pipenum = open(client_named_pipe_path, O_RDONLY);
	if (pub_pipenum == -1) {
		return -1; //failed to open pipe
	}

	if (box->n_publishers >= 1) {
		close(pub_pipenum);
		return -1; // box already has a publisher
	}

	box->n_publishers += 1;
	while (true) {
		// Reading published message from session fifo
		struct message msg;
		ssize_t n = read(pub_pipenum, &msg, sizeof(struct message));
		if (n == 0) {
			break;
		} else if (n == -1) {
			// ret == -1 indicates error
			box->n_publishers -= 1;
			close(pub_pipenum); //FIXME: estes return -1
			return -1;
		} else if (n != 0) {
			size_t len = strlen(msg.message);
			msg.message[len] = '\n';	
			// Writing in box file
			pthread_mutex_lock(&box->box_lock);
			char name[strlen(box_name)+2];
			sprintf(name, "/%s", box_name);
			int box_fd = tfs_open(name, TFS_O_APPEND);
			if (box_fd < 0) {
				box->n_publishers -= 1;
				close(pub_pipenum);
				pthread_mutex_unlock(&box->box_lock);
				return -1; // failed to open box file
			}
			
			ssize_t bytes_written = tfs_write(box_fd, msg.message, len+1);
			if (bytes_written < 0) {
				box->n_publishers -= 1;
				close(pub_pipenum);
				pthread_mutex_unlock(&box->box_lock);
				return -1; // failed to write OR write exceeded box max size
			}

			box->box_size += (uint64_t) bytes_written;
			if (tfs_close(box_fd) != 0) {
				box->n_publishers -= 1;
				close(pub_pipenum);
				pthread_mutex_unlock(&box->box_lock);
				return -1; // failed to close box file
			}
			pthread_cond_broadcast(&box->box_condvar);
			pthread_mutex_unlock(&box->box_lock);
		}
	}

	box->n_publishers -= 1;
	close(pub_pipenum);
	return 0;
}

int handle_subscriber(const char *client_named_pipe_path, const char *box_name) {
	struct box* box = lookup_box_in_list(head, box_name);
	if (box == NULL) {
		return -1; //TODO: implement worker thread response to failed handling
	}

	int sub_pipenum = open(client_named_pipe_path, O_WRONLY);
	if (sub_pipenum == -1) {
		return -1; //failed to open pipe
	}

	char name[strlen(box_name)+2];
	sprintf(name, "/%s", box_name);
	int box_fd = tfs_open(name, 0b000);
	if (box_fd < 0) {
		close(sub_pipenum);
		return -1; // failed to open file
	}

	box->n_subscribers += 1;
	while(true) {
		struct message msg_buffer = message_init(SUBSCRIBER_MESSAGE_CODE, NULL);
		memset(msg_buffer.message, 0, sizeof(msg_buffer.message));

		char message[MESSAGE_SIZE];
		memset(message, 0, MESSAGE_SIZE);

		pthread_mutex_unlock(&box->box_lock);
		while (tfs_read(box_fd, message, MESSAGE_SIZE) == 0) {
			pthread_cond_wait(&box->box_condvar, &box->box_lock);
		}
		pthread_mutex_unlock(&box->box_lock);

		if (strlen(message) == 0) {
			break; // error on reading from box
		}

		char *token = strtok(message, "\n");   
		while( token != NULL ) {
			strcpy(msg_buffer.message, token);
			ssize_t n = write(sub_pipenum, &msg_buffer, sizeof(msg_buffer));
			if (n == 0) {
				// n == 0 indicates EOF
				tfs_close(box_fd);
				box->n_subscribers -= 1;
				close(sub_pipenum); 
				break;
			} else if (n == -1) {
				// n == -1 indicates error
				box->n_subscribers -= 1;
				close(sub_pipenum);
				return -1;
			}
			token = strtok(NULL, "\n");
		}

	}

	if (tfs_close(box_fd) != 0) {
		box->n_subscribers -= 1;
		close(sub_pipenum);
		pthread_mutex_unlock(&box->box_lock);
		return -1; // failed to close box file
	}

	box->n_subscribers -= 1;
	close(sub_pipenum);
	return -1;
}

struct box_answer create_box(const char *box_name) {
	char name[strlen(box_name)+2];
	sprintf(name, "/%s", box_name); 
	int box_fd = tfs_open(name, 0b000);
	if (box_fd != -1) {
		close(box_fd);
		return box_answer_init(CREATE_BOX_ANSWER_CODE, -1, "box already exists.");
	}

	box_fd = tfs_open(name, TFS_O_CREAT);
	if (box_fd == -1) { 
		return box_answer_init(CREATE_BOX_ANSWER_CODE, -1, "unable to create box.");
	}

	if (box_count >= MAX_BOX_AMOUNT) {
		return box_answer_init(CREATE_BOX_ANSWER_CODE, -1, "unable to create box.");
	}

	struct box* new_box = (struct box*) malloc(sizeof(struct box));
	init_box(new_box, box_name);

	pthread_mutex_lock(&box_list_lock);
	
	box_count++;
	if (head == NULL) {
		head = new_box;
	} else {
		new_box->next = head;
		head = new_box;
	}

	pthread_mutex_unlock(&box_list_lock);

	return box_answer_init(CREATE_BOX_ANSWER_CODE, 0, NULL);
}

struct box_answer remove_box(const char *box_name) {

	char name[strlen(box_name)+2];
	sprintf(name, "/%s", box_name); 
	if (tfs_unlink(name) < 0 || head == NULL) {
		return box_answer_init(REMOVE_BOX_ANSWER_CODE, -1, "unable to remove box.");
	}

	pthread_mutex_lock(&box_list_lock);
	struct box* prev = NULL;
	struct box* curr = head;
	while (curr != NULL) {
		if (!strcmp(curr->box_name, box_name)) {
			if (prev == NULL) {
				head = curr->next;
			} else {
				prev->next = curr->next;
			}
			free(curr);
			break;
		}
		prev = curr;
		curr = curr->next;
	}
	box_count--;
	pthread_mutex_unlock(&box_list_lock);

	return box_answer_init(REMOVE_BOX_ANSWER_CODE, 0, NULL);
}

int list_boxes(const char *client_named_pipe_path) {

	int client_pipe = open(client_named_pipe_path, O_WRONLY);
	if (client_pipe < 0) {
		return -1;
	}

	struct box_list_entry entry;

	pthread_mutex_lock(&box_list_lock);

	if (head == NULL) {
		entry = box_list_entry_init(LIST_BOX_ANSWER_CODE, 1, NULL, 0, 0, 0);

		ssize_t n = write(client_pipe, &entry, sizeof(entry));
		if (n < 0) {
			close(client_pipe);
			pthread_mutex_unlock(&box_list_lock);
			return -1;
		}
		pthread_mutex_unlock(&box_list_lock);
		return 0;
	}	
	
	struct box* temp = head;
	for (; temp != NULL; temp = temp->next) {
		if (temp->next == NULL) {
			entry = box_list_entry_init(LIST_BOX_ANSWER_CODE, 1, temp->box_name,
										temp->box_size, temp->n_publishers,
										temp->n_subscribers);
		} else {
			entry = box_list_entry_init(LIST_BOX_ANSWER_CODE, 0, temp->box_name,
										temp->box_size, temp->n_publishers,
										temp->n_subscribers);
		}

		ssize_t n = write(client_pipe, &entry, sizeof(entry));
		if (n < 0) {
			close(client_pipe);
			pthread_mutex_unlock(&box_list_lock);
			return -1;
		}
	}

	pthread_mutex_unlock(&box_list_lock);

	close(client_pipe);
	return 0;
}

void *work(void* main_queue) {
	pc_queue_t *queue = (pc_queue_t*) main_queue;
	while (true) {

		struct basic_request *request = (struct basic_request *) pcq_dequeue(queue);

		switch (request->code) {
			case 1:
				//Pedido de registo de publisher
				handle_publisher(request->client_named_pipe_path, request->box_name);
				break;
			case 2:
				//Pedido de registo de subscriber
				handle_subscriber(request->client_named_pipe_path, request->box_name);
				break; 
			case 3: ;
				//Pedido de criação de caixa
				struct box_answer boxcreation_answer;
				boxcreation_answer = create_box(request->box_name);
				send_answer(request->client_named_pipe_path, boxcreation_answer);
				break;
			//   4: Resposta ao pedido de criação de caixa (mandado pela worker thread na subrotina)
			case 5: ;
				//Pedido de remoção de caixa
				struct box_answer boxremoval_answer;
				boxremoval_answer = remove_box(request->box_name);
				send_answer(request->client_named_pipe_path, boxremoval_answer);
				break;
			//   6: Resposta ao pedido de remoção de caixa (mandado pela worker thread na subrotina)
			case 7:
				//Pedido de listagem de caixas
				list_boxes(request->client_named_pipe_path);
				break;
			//   8: Resposta ao pedido de listagem de caixas (mandado pela worker thread na subrotina)
			default:
				continue;
		}
	}
	return NULL;
}

int new_pipe(const char *pipe_name) {
	if (unlink(pipe_name) != 0 && errno != ENOENT) {
		return -1; // failed to unlink file
	}

	if (mkfifo(pipe_name, 0640) != 0) {
		unlink(pipe_name);
		return -1; // failed to create pipe
	}

	int pipenum = open(pipe_name, O_WRONLY);
	if (pipenum == -1) {
		unlink(pipe_name);
		return -1; // failed to open pipe
	}

	return pipenum;
}

int create_server(const char *pipe_name, int num) {

	// TODO: garantir que não apaga pipes em uso maybe??
	if (unlink(pipe_name) != 0 && errno != ENOENT) {
		return -1; // failed to unlink file
	}

	if (mkfifo(pipe_name, 0640) != 0) {
		return -1; // failed to create pipe
	}

	int pipenum = open(pipe_name, O_RDONLY);
	if (pipenum == -1) {
		unlink(pipe_name);
		return -1; // failed to open pipe
	}

	// Initialize server
	if (tfs_init(NULL) < 0) {
		close(pipenum);
		unlink(pipe_name);
		exit(EXIT_FAILURE);
	}

	signal(SIGINT, sighandler);
	signal(SIGPIPE, SIG_IGN);

	pthread_mutex_init(&box_list_lock, NULL);

	pc_queue_t queue;
	pcq_create(&queue, (size_t) num*2); //change num to a different constant?
	head = NULL;

	pthread_t pid[num];
	// for testing purposes, I only want to create a single thread
	for (size_t i = 0; i < num; i++) {
		if (pthread_create(&pid[i], NULL, work, (void *)&queue) < 0) {
			tfs_destroy();
			close(pipenum);
			unlink(pipe_name);
			exit(EXIT_FAILURE);
		}
	}

	while (true) {
		struct basic_request buffer;
		ssize_t n = read(pipenum, &buffer, sizeof(struct basic_request));
		if (n == -1) {
			// ret == -1 indicates error
			break;
		} else if (n != 0) {
			pcq_enqueue(&queue, (void *)&buffer);
		}
	}

	pthread_mutex_destroy(&box_list_lock);
	pcq_destroy(&queue);
	destroy_box_list(head);
	tfs_destroy();
	close(pipenum);
	unlink(pipe_name);
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 3)
		return create_server(argv[1], atoi(argv[2]));
	else
		fprintf(stderr, "usage: mbroker <pipename>\n");

	return -1;
}
