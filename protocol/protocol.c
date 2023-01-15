#include "protocol.h"
#include <string.h>

struct basic_request basic_request_init(uint8_t code, char const *pipe_path,
										char const *box_name) {
	struct basic_request request;
	request.code = code;
	memset(request.client_named_pipe_path, 0,
		   sizeof(request.client_named_pipe_path));
	strcpy(request.client_named_pipe_path, pipe_path);
	memset(request.box_name, 0, sizeof(request.box_name));
	strcpy(request.box_name, box_name);
	return request;
}

struct message message_init(uint8_t code, char const *message) {
	struct message msg;
	msg.code = code;
	memset(msg.message, 0, sizeof(msg.message));
	strcpy(msg.message, message);
	return msg;
}

struct box_answer box_answer_init(uint8_t code, int32_t return_code,
								  char const *error_message) {
	struct box_answer answer;
	answer.code = code;
	answer.return_code = return_code;
	memset(answer.error_message, 0, sizeof(answer.error_message));
	if (error_message != NULL) {
		strcpy(answer.error_message, error_message);
	}
	return answer;
}

struct box_list_entry box_list_entry_init(uint8_t code, uint8_t last,
										  char const *box_name,
										  uint64_t box_size,
										  uint64_t n_publishers,
										  uint64_t n_subscribers) {
	struct box_list_entry entry;
	entry.code = code;
	entry.last = last;
	memset(entry.box_name, 0, sizeof(entry.box_name));
	if (box_name != NULL) {
		strcpy(entry.box_name, box_name);
	}
	entry.box_size = box_size;
	entry.n_publishers = n_publishers;
	entry.n_subscribers = n_subscribers;
	return entry;
}