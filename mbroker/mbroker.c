#include "logging.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PIPE_ERROR "failed to create pipe"

int create_server(const char *pipe_name) {
	// TODO: garantir que não apaga pipes em uso maybe??
	if (unlink(pipe_name) != 0 && errno != ENOENT) {
		return -1; // failed to unlink file
	}

	if (mkfifo(pipe_name, 0640) != 0) {
		return -1; // failed to create pipe
	}

	int pipenum = open(pipe_name, O_RDONLY);
	if (pipenum == -1) {
		return -1; // failed to open pipe
	}
	close(pipenum);
    unlink(pipe_name);
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 2) create_server(argv[1]);
	fprintf(stderr, "usage: mbroker <pipename>\n");

	return -1;
}
