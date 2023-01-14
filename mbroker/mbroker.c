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

#define BUFFER_SIZE 128
#define CREATE_BOX_ANSWER_CODE 4
#define REMOVE_BOX_ANSWER_CODE 6
#define LIST_BOX_ANSWER_CODE 8

struct box {
	char box_name[32];
	int n_publishers;
	int n_subscribers;
	struct box* next;
};

struct box* head;

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

void add_box_to_list(struct box* head_box, struct box* new_box) {
	if (head_box->next == NULL) {
		head_box->next = new_box;
		return;
	} else {
		return add_box_to_list(head_box->next, new_box);
	}
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

int handle_publisher(const char *client_named_pipe_path, const char *box_name) {
	//TODO: implement
	(void) client_named_pipe_path;
	(void) box_name;
	return 0;
}

int handle_subscriber(const char *client_named_pipe_path, const char *box_name) {
	//TODO: implement
	(void) client_named_pipe_path;
	(void) box_name;
	return 0;
}

struct box_answer create_box(const char *box_name) {
	// FIXME: garantir que se só se cria se a box não existir(se já existir deve falhar)
	size_t n = strlen(box_name)+2;
	char name[n];
	sprintf(name, "/%s", box_name);
	
	int box_fd = tfs_open(name, TFS_O_CREAT);
	if (box_fd == -1) {
		return box_answer_init(CREATE_BOX_ANSWER_CODE, -1, "unable to create box.");
	}

	struct box* new_box = (struct box*) malloc(sizeof(struct box));
	strcpy(new_box->box_name, box_name);
	new_box->next = NULL;

	if (head == NULL) {
		head = new_box;
	} else {
		add_box_to_list(head, new_box);
	}

	return box_answer_init(CREATE_BOX_ANSWER_CODE, 0, NULL);
}

int remove_box(const char *client_named_pipe_path, const char *box_name) {
	(void) client_named_pipe_path; //will implement responses tomorrow;

	if (tfs_unlink(box_name) < 0) {
		return -1;
	}

	if (head == NULL) {
		return -1;
	}

	if (!strcmp(head->box_name, box_name)) {
		struct box* temp = head;
		head = head->next;
		free(temp->box_name);
		free(temp);
	} else {
		remove_box_from_list(head, box_name);
	}
	return 0;
}

int list_boxes(const char *client_named_pipe_path) {

	int client_pipe = open(client_named_pipe_path, O_WRONLY);
	if (client_pipe < 0) {
		return -1;
	}

	if (head == NULL) {
		struct box_list_entry ble;
		ble.code = 8;
		ble.last = 1;
		memset(ble.box_name, '\0', sizeof(ble.box_name));
		ble.n_publishers = 1; //TODO: implement
		ble.n_subscribers = 1; //TODO: implement

		ssize_t n = write(client_pipe, &ble, sizeof(ble));
		if (n < 0) {
			close(client_pipe);
			return -1;
		}
		return 0;
	}	
	
	for(; head != NULL; head = head->next) {
		struct box_list_entry ble;
		ble.code = 8;
		if (head->next == NULL) {
			ble.last = 1;
		} else {
			ble.last = 0;
		}
		strcpy(ble.box_name, head->box_name);
		ble.box_size = 1; //TODO: implement
		ble.n_publishers = 1; //TODO: implement
		ble.n_subscribers = 1; //TODO: implement

		ssize_t n = write(client_pipe, &ble, sizeof(ble));
		if (n < 0) {
			close(client_pipe);
			return -1;
		}
	}
	close(client_pipe);
	return 0;
}

void *work(void* main_queue) {
	pc_queue_t *queue = (pc_queue_t*) main_queue;
	while (true) {
		//wait for condvar? qual? im confusion

		//??
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
			case 3:
				//Pedido de criação de caixa
				struct box_answer answer = create_box(request->box_name);
				send_answer(request->client_named_pipe_path, answer);
				break;
			//   4: Resposta ao pedido de criação de caixa (mandado pela worker thread na subrotina)
			case 5:
				//Pedido de remoção de caixa
				result = remove_box(request->client_named_pipe_path, request->box_name);
				break;
			//   6: Resposta ao pedido de remoção de caixa (mandado pela worker thread na subrotina)
			case 7:
				//Pedido de listagem de caixas
				result = list_boxes(request->client_named_pipe_path);

				break;
			//   8: Resposta ao pedido de listagem de caixas (mandado pela worker thread na subrotina)
			default:
				continue;
		}
		(void) result;
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

	while (true) {
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

	pthread_join(pid[0], NULL);

	fprintf(stderr, "[INFO]: closing pipe\n");
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
