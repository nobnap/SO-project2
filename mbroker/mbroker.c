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
#include "protocol.h"
#include "operations.h"
#include <signal.h>

#define BUFFER_SIZE 128
#define MESSAGE_SIZE 1024
#define MAX_BOX_AMOUNT 128
#define CREATE_BOX_ANSWER_CODE 4
#define REMOVE_BOX_ANSWER_CODE 6
#define LIST_BOX_ANSWER_CODE 8

struct box {
	char box_name[32];
	int box_id;
	uint64_t n_publishers;
	uint64_t n_subscribers;
	uint64_t box_size; //maybe remove this?
	struct box* next;
};

// Global box head struct: used as the start of a global box linked list.
struct box* head;

// Global flag variable: used to exit out of the main thread loop when a signal is received.
int flag = 0;

// Global counting variable: used to assign unique IDs to box objects
static int box_count = 0;

static pthread_mutex_t *box_lock;
static pthread_cond_t *box_condvar;
static pthread_mutex_t box_list_lock;

static void sighandler(int sig) {

  if (sig == SIGINT) {
    // In some systems, after the handler call the signal gets reverted
    // to SIG_DFL (the default action associated with the signal).
    // So we set the signal handler back to our function after each trap.
    //
    if (signal(SIGINT, sighandler) == SIG_ERR)
      exit(EXIT_FAILURE);
    
	flag = 1;
    return; // Resume execution at point of interruption
  }

  //Caught SIGQUIT - should we send a proper message?
  exit(EXIT_SUCCESS);

}

void send_msg(int tx) {
	char const *str = "WOW, A MESSAGE";
	size_t len = strlen(str);
	ssize_t written = 0;

	while (written < len) {
		ssize_t ret = write(tx, str + written, len - (size_t)written);
		if (ret < 0) {
			fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		written += ret;
	}
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

struct box_answer box_answer_init(uint8_t code, int32_t return_code, char *error_message) {
	struct box_answer answer;
	answer.code = code;
	answer.return_code = return_code;
	memset(answer.error_message, 0, sizeof(answer.error_message));
	if (error_message != NULL) {
		strcpy(answer.error_message, error_message);
	}
	return answer;
}

//void add_box_to_list(struct box* head_box, struct box* new_box) {
//  	if (head_box->next == NULL) {
//  		head_box->next = new_box;
//  		return;
//  	} else {
//  		return add_box_to_list(head_box->next, new_box);
//  	}
//}

void add_box_to_list(struct box* new_box) {
	new_box->next = head;
	head = new_box;
}

void remove_box_from_list(struct box* head_box, const char* box_name) {
	if (head_box->next == NULL) return;
	if (!strcmp(head_box->next->box_name, box_name)) {
		struct box* temp = head_box->next;
		head_box->next = temp->next;
		free(temp->box_name);
		free(temp);
		return;
	} else {
		return remove_box_from_list(head_box->next, box_name);
	}
}

struct box* lookup_box_in_list(struct box* head_box, const char* box_name) {
	if (head_box == NULL) {
		return NULL;
	}
	if (!strcmp(box_name, head_box->box_name)) {
		return head;
	} else {
		return lookup_box_in_list(head_box->next, box_name);
	}

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

	box->n_publishers += 1;
	while (true) {

		// do i even need to add a wait on condvar here? read func should auto block the thread hopefully

		// Reading published message from session fifo
		char msg_buffer[MESSAGE_SIZE];
		memset(msg_buffer, 0, MESSAGE_SIZE);
		ssize_t n = read(pub_pipenum, msg_buffer, MESSAGE_SIZE);
		if (n == 0) {
			// n == 0 indicates EOF
			fprintf(stderr, "[INFO]: pipe closed\n");
			box->n_publishers -= 1;
			//close(pub_pipenum); ?
			return 0;
		} else if (n == -1) {
			// ret == -1 indicates error
			box->n_publishers -= 1;
			close(pub_pipenum);
			return -1;
		} else if (n != 0) {

			// Writing in box file
			pthread_mutex_lock(&box_lock[box->box_id]);
			int box_fd = tfs_open(box_name, TFS_O_APPEND);
			if (box_fd < 0) {
				box->n_publishers -= 1;
				close(pub_pipenum);
				pthread_mutex_unlock(&box_lock[box->box_id]);
				return -1; // failed to open box file
			}
			// PLS CHECK THIS PART
			ssize_t bytes_written = tfs_write(box_fd, msg_buffer, MESSAGE_SIZE);
			if (bytes_written < MESSAGE_SIZE || bytes_written < 0) {
				box->n_publishers -= 1;
				close(pub_pipenum);
				pthread_mutex_unlock(&box_lock[box->box_id]);
				return -1; // failed to write OR write exceeded box max size
			}
			box->box_size += (uint64_t) bytes_written;
			if (tfs_close(box_fd) != 0) {
				box->n_publishers -= 1;
				close(pub_pipenum);
				pthread_mutex_unlock(&box_lock[box->box_id]);
				return -1; // failed to close box file
			}
			pthread_cond_broadcast(&box_condvar[box->box_id]);
			pthread_mutex_unlock(&box_lock[box->box_id]);
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

	box->n_subscribers += 1;
	while (true) {

		int to_read = box->box_size; //TODO: fix the wait condition

		pthread_mutex_lock(&box_lock[box->box_id]);
		
		while(to_read == 0) { //TODO: fix the wait condition
			pthread_cond_wait(&box_condvar[box->box_id], &box_lock[box->box_id]);
		}

		// Reading from box file
		char msg_buffer[MESSAGE_SIZE];
		memset(msg_buffer, 0, MESSAGE_SIZE);
		int box_fd = tfs_open(box_name, TFS_O_APPEND);
		if (box_fd < 0) {
			box->n_subscribers -= 1;
			close(sub_pipenum);
			pthread_mutex_unlock(&box_lock[box->box_id]);
			return -1; // failed to open box file
		}
		ssize_t bytes_read = tfs_read(box_fd, msg_buffer, MESSAGE_SIZE);
		if (bytes_read < 0) {
			box->n_subscribers -= 1;
			close(sub_pipenum);
			pthread_mutex_unlock(&box_lock[box->box_id]);
			return -1; // failed to read from file
		}

		to_read -= bytes_read; //TODO: fix the wait condition

		if (tfs_close(box_fd) != 0) {
			box->n_subscribers -= 1;
			close(sub_pipenum);
			pthread_mutex_unlock(&box_lock[box->box_id]);
			return -1; // failed to close box file
		}
		pthread_mutex_unlock(&box_lock[box->box_id]);

		// Sending read results to subscriber thread through pipe
		if (bytes_read < MESSAGE_SIZE) msg_buffer[bytes_read] = 0; //adding a \0 to the end just in case
		ssize_t n = write(sub_pipenum, msg_buffer, MESSAGE_SIZE);
		if (n == 0) {
			// n == 0 indicates EOF
			fprintf(stderr, "[INFO]: pipe closed\n");
			box->n_subscribers -= 1;
			// close(sub_pipenum); we only close the pipe whenever the thread is killed so no
			return 0;
		} else if (n == -1) {
			// ret == -1 indicates error
			box->n_subscribers -= 1;
			close(sub_pipenum);
			return -1;
		}
	}

	box->n_subscribers -= 1;
	close(sub_pipenum);
	return 0;
}

struct box_answer create_box(const char *box_name) {
	// FIXME: garantir que se só se cria se a box não existir(se já existir deve falhar)
	size_t n = strlen(box_name)+2;
	char name[n];
	sprintf(name, "/%s", box_name);
	
	int box_fd = tfs_open(name, TFS_O_CREAT);
	if (box_fd == -1 || box_count >= MAX_BOX_AMOUNT) { // TODO: figure out if the second condition is correct
		return box_answer_init(CREATE_BOX_ANSWER_CODE, -1, "unable to create box.");
	}

	struct box* new_box = (struct box*) malloc(sizeof(struct box));
	strcpy(new_box->box_name, box_name);
	new_box->next = NULL;
	new_box->box_size = 0;
	new_box->n_publishers = 0;
	new_box->n_subscribers = 0;
	new_box->box_id = box_count;
	box_count++;

	pthread_mutex_lock(&box_list_lock);
	add_box_to_list(new_box);
	pthread_mutex_unlock(&box_list_lock);

	return box_answer_init(CREATE_BOX_ANSWER_CODE, 0, NULL);
}

struct box_answer remove_box(const char *box_name) {

	if (tfs_unlink(box_name) < 0) {
		return box_answer_init(REMOVE_BOX_ANSWER_CODE, -1, "unable to create box.");
	}

	if (head == NULL) {
		return box_answer_init(REMOVE_BOX_ANSWER_CODE, -1, "unable to create box.");
	}

	if (!strcmp(head->box_name, box_name)) {
		pthread_mutex_lock(&box_list_lock);
		struct box* temp = head;
		head = head->next;
		free(temp->box_name);
		free(temp);
		pthread_mutex_unlock(&box_list_lock);
	} else {
		pthread_mutex_lock(&box_list_lock);
		remove_box_from_list(head, box_name);
		pthread_mutex_unlock(&box_list_lock);
	}
	return box_answer_init(REMOVE_BOX_ANSWER_CODE, 0, NULL);
}

int list_boxes(const char *client_named_pipe_path) {

	int client_pipe = open(client_named_pipe_path, O_WRONLY);
	if (client_pipe < 0) {
		return -1;
	}

	struct box_list_entry *entry = (struct box_list_entry*) malloc(sizeof(struct box_list_entry));
	entry->code = LIST_BOX_ANSWER_CODE;

	pthread_mutex_lock(&box_list_lock);

	if (head == NULL) {
		entry->last = 1;
		memset(entry->box_name, '\0', sizeof(entry->box_name));
		entry->n_publishers = 0;
		entry->n_subscribers = 0;

		ssize_t n = write(client_pipe, entry, sizeof(entry));
		if (n < 0) {
			close(client_pipe);
			pthread_mutex_unlock(&box_list_lock);
			return -1;
		}
		pthread_mutex_unlock(&box_list_lock);
		return 0;
	}	
	
	for(; head != NULL; head = head->next) {
		if (head->next == NULL) {
			entry->last = 1;
		} else {
			entry->last = 0;
		}
		strcpy(entry->box_name, head->box_name);
		entry->box_size = head->box_size;
		entry->n_publishers = head->n_publishers;
		entry->n_subscribers = head->n_subscribers;

		ssize_t n = write(client_pipe, entry, sizeof(entry));
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

		int result;

		switch (request->code) {
			case 1:
				//Pedido de registo de publisher
				result = handle_publisher(request->client_named_pipe_path, request->box_name);
				break;
			case 2:
				//Pedido de registo de subscriber
				result = handle_subscriber(request->client_named_pipe_path, request->box_name);
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
				result = list_boxes(request->client_named_pipe_path);
				break;
			//   8: Resposta ao pedido de listagem de caixas (mandado pela worker thread na subrotina)
			default:
				result = -1;
		}
		(void) result; //figure out what to do with this
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

	for(int i = 0; i < MAX_BOX_AMOUNT; i++) {
		pthread_mutex_init(&box_lock[i], NULL);
		pthread_cond_init(&box_condvar[i], NULL);
	}

	pc_queue_t queue;
	pcq_create(&queue, (size_t) num); //change num to a different constant?

	pthread_t pid[num];
	// for testing purposes, I only want to create a single thread
	if (pthread_create(&pid[0], NULL, work, (void *)&queue) < 0) {
		tfs_destroy();
		close(pipenum);
		unlink(pipe_name);
		fprintf(stderr, "failed to create thread: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	head = NULL;

	// flag switches to 1 when the thread receives SIGINT
	while (flag == 0) {
		struct basic_request* buffer = (struct basic_request*)malloc(sizeof(struct basic_request));
		ssize_t n = read(pipenum, buffer, sizeof(struct basic_request));
		if (n == -1) {
			// ret == -1 indicates error
			break;
		} else if (n != 0) {
			printf("REQUEST: %i\nPIPE: %s\nBOX: %s\n", buffer->code, buffer->client_named_pipe_path, buffer->box_name);
			pcq_enqueue(&queue, (void *)buffer);
			break;
		}
	}

	//using this instead of kill
	pthread_kill(pid[0], SIGINT);

	pthread_join(pid[0], NULL);

	fprintf(stderr, "[INFO]: closing pipe\n");

	pthread_mutex_destroy(&box_list_lock);

	for(int i = 0; i < MAX_BOX_AMOUNT; i++) {
		pthread_mutex_destroy(&box_lock[i]);
		pthread_cond_destroy(&box_condvar[i]);
	}

	tfs_destroy();
	close(pipenum);
	unlink(pipe_name);
	fprintf(stderr, "[INFO]: server was deleted\n");
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 3)
		return create_server(argv[1], atoi(argv[2]));
	else
		fprintf(stderr, "usage: mbroker <pipename>\n");

	return -1;
}
