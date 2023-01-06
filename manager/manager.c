#include "logging.h"
#include <string.h>

static void print_usage() {
	fprintf(stderr, "usage: \n"
					"   manager <register_pipe_name> create <box_name>\n"
					"   manager <register_pipe_name> remove <box_name>\n"
					"   manager <register_pipe_name> list\n");
}

int list_boxes(const char *pipe_name) {
	(void)pipe_name;
	fprintf(stderr, "listing boxes...\n");
	WARN("unimplemented"); // TODO: implement
	return 0;
}

int create_box(const char *pipe_name, const char *box_name) {
	(void)pipe_name;
	(void)box_name;
	fprintf(stderr, "creating boxes...\n");
	WARN("unimplemented"); // TODO: implement
	return 0;
}

int remove_box(const char *pipe_name, const char *box_name) {
	(void)pipe_name;
	(void)box_name;
	fprintf(stderr, "removing boxes...\n");
	WARN("unimplemented"); // TODO: implement
	return 0;
}

int main(int argc, char **argv) {

	switch (argc) {
	case 3:
		if (!strcmp(argv[2], "list"))
			return list_boxes(argv[1]);
		print_usage();
		break;
	case 4:
		if (!strcmp(argv[2], "create"))
			return create_box(argv[1], argv[3]);
		else if (!strcmp(argv[2], "remove"))
			return remove_box(argv[1], argv[3]);
		print_usage();
		break;
	default:
		print_usage();
	}

	return -1;
}
