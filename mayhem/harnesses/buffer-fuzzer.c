/*
 * libFuzzer port of test/fuzz/buffer-fuzzer.c (an AFL stdin driver written against an older Buffer
 * API): the same line-oriented command interpreter, driven from the fuzz input.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
/* vis is a unity build (see main.c / test/core): util.h first, then the .c files */
#include "util.h"
#include "util.c"
#include "buffer.c"

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

enum CmdStatus { CMD_FAIL, CMD_OK, CMD_ERR, CMD_QUIT };

typedef enum CmdStatus (*Cmd)(Buffer *buf, const char *cmd);

static enum CmdStatus cmd_insert(Buffer *buf, const char *cmd) {
	char data[BUFSIZ];
	size_t pos;
	if (sscanf(cmd, "%zu %s\n", &pos, data) != 2)
		return CMD_ERR;
	return vis_buffer_insert(buf, pos, data, strlen(data) + 1);
}

static enum CmdStatus cmd_set(Buffer *buf, const char *cmd) {
	char data[BUFSIZ];
	if (sscanf(cmd, "%s\n", data) != 1)
		return CMD_ERR;
	return buffer_put(buf, data, strlen(data) + 1);
}

static enum CmdStatus cmd_append(Buffer *buf, const char *cmd) {
	char data[BUFSIZ];
	if (sscanf(cmd, "%s\n", data) != 1)
		return CMD_ERR;
	return vis_buffer_append0(buf, data);
}

static enum CmdStatus cmd_appendf(Buffer *buf, const char *cmd) {
	char data[BUFSIZ];
	if (sscanf(cmd, "%s\n", data) != 1)
		return CMD_ERR;
	return vis_buffer_appendf(buf, "<%s:%zu>", data, strlen(data));
}

static enum CmdStatus cmd_delete(Buffer *buf, const char *cmd) {
	size_t pos, len;
	if (sscanf(cmd, "%zu %zu", &pos, &len) != 2)
		return CMD_ERR;
	return buffer_remove(buf, pos, len);
}

static enum CmdStatus cmd_clear(Buffer *buf, const char *cmd) {
	buffer_release(buf);
	return CMD_OK;
}

static enum CmdStatus cmd_size(Buffer *buf, const char *cmd) {
	printf("%zu bytes\n", (size_t)buf->length);
	return CMD_OK;
}

static enum CmdStatus cmd_capacity(Buffer *buf, const char *cmd) {
	printf("%zu bytes\n", (size_t)buf->size);
	return CMD_OK;
}

static enum CmdStatus cmd_print(Buffer *buf, const char *cmd) {
	const char *data = buffer_content0(buf);
	return puts(data) >= 0;
}

static enum CmdStatus cmd_quit(Buffer *buf, const char *cmd) {
	return CMD_QUIT;
}

static Cmd commands[] = {
	['?'] = cmd_capacity,
	['='] = cmd_set,
	['#'] = cmd_size,
	['+'] = cmd_append,
	['f'] = cmd_appendf,
	['c'] = cmd_clear,
	['d'] = cmd_delete,
	['i'] = cmd_insert,
	['p'] = cmd_print,
	['q'] = cmd_quit,
};

static int repl(FILE *input) {
	char line[BUFSIZ];
	Buffer buf = {0};

	while (fgets(line, sizeof(line), input)) {
		if (line[0] == '\n')
			continue;
		size_t idx = (unsigned char)line[0];
		if (idx < LENGTH(commands) && commands[idx]) {
			if (commands[idx](&buf, line + 1) == CMD_QUIT)
				break;
		}
	}

	buffer_release(&buf);
	return 0;
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	/* the interpreter echoes results; drop them so the fuzz loop is not I/O bound */
	if (!freopen("/dev/null", "w", stdout))
		return 1;
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len) {
	if (len == 0)
		return -1;
	FILE *input = fmemopen((void *)data, len, "r");
	if (!input)
		return -1;
	repl(input);
	fclose(input);
	return 0;
}
