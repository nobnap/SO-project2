#include "logging.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 128

void handle() {
	fprintf(stdout, "\nReceived messages.\n");
	exit(EXIT_SUCCESS);
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

	while (access(pipe_name, F_OK) == -1) { /* nothing happens */}

	int pipenum = open(pipe_name, O_RDONLY);
	if (pipenum == -1) {
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

	return 0;
}

int main(int argc, char **argv) {
	if (argc == 4)
		return subscribe_box(argv[1], argv[2], argv[3]);
	fprintf(stderr, "usage: sub <register_pipe_name> <pipe_name> <box_name>\n");

	// github test msg
	return -1;
}
