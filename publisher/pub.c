#include "logging.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#define BUFFER_SIZE 1024

int pipenum = -1;

void handle() {
	if (pipenum > 0) {
		close(pipenum);
	}
	fprintf(stdout, "\nPublished messages.\n");
	exit(EXIT_SUCCESS);
}

int new_pipe(const char *pipe_name) {
	// FIXME: se já existir pipe com este nome, algo tem de acontecer
	// (e não é isto)
	if (unlink(pipe_name) != 0 && errno != ENOENT) {
		return -1; // failed to unlink file
	}

	if (mkfifo(pipe_name, 0640) != 0) {
		unlink(pipe_name);
		return -1; // failed to create pipe
	}

	return 0;
}

int send_request(const char *server_pipe, struct basic_request request) {
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
	return 0;
}

int publish_message(const char *server_pipe, const char *pipe_name, 
					const char *box_name) {
	struct basic_request request;
	request.code = 1;
	strcpy(request.client_named_pipe_path, pipe_name);
	strcpy(request.box_name, box_name);

	fprintf(stderr, "subscribing to box...\nPIPE_NAME: %s\nBOX_NAME: %s\n",
			request.client_named_pipe_path, request.box_name);

	signal(SIGPIPE, handle);
	signal(SIGINT, handle);

	if (send_request(server_pipe, request) == -1) {
		return -1;
	}

	if (new_pipe(pipe_name) == -1) {
		return -1;
	}

	pipenum = open(pipe_name, O_WRONLY);
	if (pipenum == -1) {
		unlink(pipe_name);
		return -1; // failed to open pipe
	}

	while (true) {
		char buffer[BUFFER_SIZE];
		memset(buffer, 0, BUFFER_SIZE);

		while(fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
			buffer[BUFFER_SIZE-1] = '\0';
			struct message msg;
			msg.code = 9;
			strcpy(msg.message, buffer);

			ssize_t n = write(pipenum, &msg, sizeof(msg));
			if (n < 0) {
				close(pipenum);
				unlink(pipe_name);
				return -1;
			}	

		}
	}

	//desnecessario?
	close(pipenum);
	unlink(pipe_name);
	return 0;
}

int main(int argc, char **argv) {

	if (argc == 4)
		return publish_message(argv[1], argv[2], argv[3]);
	fprintf(stderr, "usage: pub <register_pipe_name> <box_name>\n");

	return -1;
}