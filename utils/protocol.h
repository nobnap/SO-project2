#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <stdint.h>

struct __attribute__ ((__packed__)) basic_request {
    uint8_t code;
    char client_named_pipe_path[256];
	char box_name[32];
};

struct __attribute__ ((__packed__)) message {
    uint8_t code;
	char message[1024];
};

struct __attribute__ ((__packed__)) box_answer {
    uint8_t code;
    int32_t return_code;
	char error_message[1024];
};

struct __attribute__ ((__packed__)) box_list_request {
    uint8_t code;
    char client_named_pipe_path[256];
};

struct __attribute__ ((__packed__)) box_list_entry {
    uint8_t code;
	uint8_t last;
    char box_name[32];
	uint64_t box_size;
	uint64_t n_publishers;
	uint64_t n_subscribers;
};

#endif