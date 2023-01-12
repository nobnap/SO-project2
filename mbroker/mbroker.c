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

#define BUFFER_SIZE 128

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

	send_msg(pipenum);

	fprintf(stderr, "[INFO]: closing pipe\n");
	close(pipenum);
	unlink(pipe_name);
	return 0;
}

int create_server(const char *pipe_name, int num) {
	(void)num;
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

	while (true) {
		struct basic_request buffer;
		ssize_t n = read(pipenum, &buffer, sizeof(buffer));
		if (n == 0) {
			// ret == 0 indicates EOF
			break;
		} else if (n == -1) {
			// ret == -1 indicates error
			break;
		} else if (n != 0) {
			printf("REQUEST: %i\nPIPE: %s\nBOX: %s\n", buffer.code, buffer.client_named_pipe_path, buffer.box_name);
			new_pipe(buffer.client_named_pipe_path);
		}
	}

	fprintf(stderr, "[INFO]: closing pipe\n");
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
