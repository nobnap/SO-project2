#include "logging.h"

int subscribe_box(const char *pipe_name, const char *box_name) {
	(void)pipe_name;
	(void)box_name;
	fprintf(stderr, "subscribing to box...\n");
	WARN("unimplemented"); // TODO: implement
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 3)
		return subscribe_box(argv[1], argv[2]);
	fprintf(stderr, "usage: sub <register_pipe_name> <box_name>\n");

	return -1;
}
