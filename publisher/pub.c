#include "logging.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

int pipenum = -1;

void handle() {
	if (pipenum > 0) {
		close(pipenum);
	}
	fprintf(stdout, "\nPublished messages.\n");
	exit(EXIT_SUCCESS);
}

int publish_message(const char *server_pipe, const char *pipe_name, 
					const char *box_name) {
	struct basic_request request;
	request.code = 1;
	strcpy(request.client_named_pipe_path, pipe_name);
	strcpy(request.box_name, box_name);

	fprintf(stderr, "subscribing to box...\nPIPE_NAME: %s\nBOX_NAME: %s\n",
			request.client_named_pipe_path, request.box_name);

	signal(SIGINT, handle);

	int server = open(server_pipe, O_WRONLY);
	if (server == -1) {
		return -1; // failed to open pipe
	}

	ssize_t ret = write(server, &request, sizeof(request));
	if (ret < 0) {
		fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	close(server);

	printf("REQUEST SENT\n");

	while (access(pipe_name, F_OK) == -1) { /* nothing happens */ }

	pipenum = open(pipe_name, O_WRONLY);
	if (pipenum == -1) {
		return -1; // failed to open pipe
	}

	while (true) {
		// deviamos fazer um mecanismo que parte um input do stdin (caso for demasiado grande) em várias mensagens e manda-as todas em ciclo?
		// neste momento, só truncamos e acabou
		char buffer[BUFFER_SIZE];
		memset(buffer, 0, BUFFER_SIZE);
		ssize_t input = read(STDIN_FILENO, buffer, BUFFER_SIZE -1);
		if (input < 0) {
			fprintf(stderr, "[ERR]: read from stdin failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		buffer[BUFFER_SIZE-1] = "\0";

		struct message msg;
		msg.code = 9;
		strcpy(msg.message, buffer);

		ssize_t n = write(pipenum, &msg, sizeof(msg));
		if (n < 0) {
			return -1;
		}

	}

	//desnecessario?
	close(pipenum);

	return 0;
}

int main(int argc, char **argv) {

	if (argc == 4)
		return publish_message(argv[1], argv[2], argv[3]);
	fprintf(stderr, "usage: pub <register_pipe_name> <box_name>\n");

	return -1;
}