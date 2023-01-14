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

#define BUFFER_SIZE 128

void handle() {
	// TODO: implement proper closing
	fprintf(stdout, "\nReceived messages.\n");
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

int subscribe_box(const char *server_pipe, const char *pipe_name,
				  const char *box_name) {
	struct basic_request request;
	request.code = 2;
	strcpy(request.client_named_pipe_path, pipe_name);
	strcpy(request.box_name, box_name);

	fprintf(stderr, "subscribing to box...\nPIPE_NAME: %s\nBOX_NAME: %s\n",
			request.client_named_pipe_path, request.box_name);

	signal(SIGINT, handle);

	if (send_request(server_pipe, request) == -1) {
		return -1;
	}

	if (new_pipe(pipe_name) == -1) {
		return -1;
	}

	int pipenum = open(pipe_name, O_RDONLY);
	if (pipenum == -1) {
		unlink(pipe_name);
		return -1; // failed to open pipe
	}

	while (true) {
		char buffer[BUFFER_SIZE];
		memset(buffer, 0, BUFFER_SIZE);
		ssize_t n = read(pipenum, buffer, BUFFER_SIZE - 1);
		if (n == 0) {
			// ret == 0 indicates EOF
			fprintf(stderr, "[INFO]: pipe closed\n");
			return 0;
		} else if (n == -1) {
			// ret == -1 indicates error
			return -1;
		} else if (n != 0) {
			buffer[n] = 0;
			fprintf(stdout, "%s\n", buffer);
		}
	}

	close(pipenum);
	unlink(pipe_name);
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 4)
		return subscribe_box(argv[1], argv[2], argv[3]);
	fprintf(stderr, "usage: sub <register_pipe_name> <pipe_name> <box_name>\n");

	return -1;
}
