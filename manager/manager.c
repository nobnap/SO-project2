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

#define CREATE_BOX_REQUEST_CODE 3
#define REMOVE_BOX_REQUEST_CODE 5
#define LIST_BOX_REQUEST_CODE 7

static void print_usage() {
	fprintf(stderr,
			"usage: \n"
			"   manager <register_pipe_name> <pipe_name> create <box_name>\n"
			"   manager <register_pipe_name> <pipe_name> remove <box_name>\n"
			"   manager <register_pipe_name> <pipe_name> list\n");
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
		return -1;
	}

	close(server);
	return 0;
}

int list_boxes(const char *server_pipe, const char *pipe_name) {
	struct basic_request request = basic_request_init(LIST_BOX_REQUEST_CODE,
													  pipe_name, NULL);

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

	struct box_list_entry buffer;
	do {
		ssize_t n = read(pipenum, &buffer, sizeof(buffer));
		if (n == -1) {
			// ret == -1 indicates error
			break;
		} else if (n != 0) {
			if (buffer.box_name[0] == 0)
				fprintf(stdout, "NO BOXES FOUND\n");
			else
				fprintf(stdout, "%s %zu %zu %zu\n", buffer.box_name,
						buffer.box_size, buffer.n_publishers,
						buffer.n_subscribers);
		}
	} while (buffer.last != 1);

	close(pipenum);
	unlink(pipe_name);
	return 0;
}

int create_box(const char *server_pipe, const char *pipe_name,
			   const char *box_name) {
	struct basic_request request = basic_request_init(CREATE_BOX_REQUEST_CODE,
													  pipe_name, box_name);

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
		struct box_answer buffer;
		ssize_t n = read(pipenum, &buffer, sizeof(buffer));
		if (n == -1) {
			// ret == -1 indicates error
			break;
		} else if (n != 0) {
			if (buffer.return_code == 0)
				fprintf(stdout, "OK\n");
			else
				fprintf(stdout, "ERROR %s\n", buffer.error_message);
			break;
		}
	}

	close(pipenum);
	unlink(pipe_name);
	return 0;
}

int remove_box(const char *server_pipe, const char *pipe_name,
			   const char *box_name) {
	struct basic_request request = basic_request_init(REMOVE_BOX_REQUEST_CODE,
													  pipe_name, box_name);

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
		struct box_answer buffer;
		ssize_t n = read(pipenum, &buffer, sizeof(buffer));
		if (n == 0) {
			// ret == 0 indicates EOF
			break;
		} else if (n == -1) {
			// ret == -1 indicates error
			break;
		} else if (n != 0) {
			if (buffer.return_code == 0)
				fprintf(stdout, "OK\n");
			else
				fprintf(stdout, "ERROR %s\n", buffer.error_message);
		}
	}

	close(pipenum);
	unlink(pipe_name);
	return 0;
}

int main(int argc, char **argv) {

	switch (argc) {
	case 4:
		if (!strcmp(argv[3], "list"))
			return list_boxes(argv[1], argv[2]);
		break;
	case 5:
		if (!strcmp(argv[3], "create"))
			return create_box(argv[1], argv[2], argv[4]);
		else if (!strcmp(argv[3], "remove"))
			return remove_box(argv[1], argv[2], argv[4]);
		break;
	default:
		break;
	}
	print_usage();

	return -1;
}
