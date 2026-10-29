/*
 * libFuzzer port of test/fuzz/text-fuzzer.c (an AFL/libFuzzer stdin driver written against an older
 * Text API): the same line-oriented command interpreter over a Text instance, adapted to the current
 * API. The timing-based `bench` command is dropped; `m`/`o` commands drive the motion and text-object
 * code paths that the editor reaches from user keystrokes.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* vis is a unity build (see main.c / test/core): util.h first, then the .c files */
#include "util.h"
#include "util.c"
#include "buffer.c"
#include "text.c"

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

enum CmdStatus { CMD_FAIL, CMD_OK, CMD_ERR, CMD_QUIT };

typedef enum CmdStatus (*Cmd)(Text *txt, const char *cmd);

static Mark mark;

static size_t unescape(char *s) {
	char *w = s;
	for (const char *r = s; *r; r++) {
		if (r[0] == '\\' && r[1] == 'n') {
			*w++ = '\n';
			r++;
		} else if (r[0] == '\\' && r[1] == 't') {
			*w++ = '\t';
			r++;
		} else {
			*w++ = *r;
		}
	}
	*w = '\0';
	return w - s;
}

static enum CmdStatus cmd_insert(Text *txt, const char *cmd) {
	char data[BUFSIZ];
	size_t pos;
	if (sscanf(cmd, "%zu %s\n", &pos, data) != 2)
		return CMD_ERR;
	size_t len = unescape(data);
	return text_insert(NULL, txt, pos, data, len);
}

static enum CmdStatus cmd_delete(Text *txt, const char *cmd) {
	size_t pos, len;
	if (sscanf(cmd, "%zu %zu", &pos, &len) != 2)
		return CMD_ERR;
	return text_delete(txt, pos, len);
}

static enum CmdStatus cmd_size(Text *txt, const char *cmd) {
	printf("%zu bytes\n", text_size(txt));
	return CMD_OK;
}

static enum CmdStatus cmd_snapshot(Text *txt, const char *cmd) {
	text_snapshot(txt);
	return CMD_OK;
}

static enum CmdStatus cmd_undo(Text *txt, const char *cmd) {
	return text_undo(txt) != EPOS;
}

static enum CmdStatus cmd_redo(Text *txt, const char *cmd) {
	return text_redo(txt) != EPOS;
}

static enum CmdStatus cmd_earlier(Text *txt, const char *cmd) {
	return text_earlier(txt) != EPOS;
}

static enum CmdStatus cmd_later(Text *txt, const char *cmd) {
	return text_later(txt) != EPOS;
}

static enum CmdStatus cmd_mark_set(Text *txt, const char *cmd) {
	size_t pos;
	if (sscanf(cmd, "%zu\n", &pos) != 1)
		return CMD_ERR;
	Mark m = text_mark_set(txt, pos);
	if (m != EMARK)
		mark = m;
	return m != EMARK;
}

static enum CmdStatus cmd_mark_get(Text *txt, const char *cmd) {
	size_t pos = text_mark_get(txt, mark);
	if (pos != EPOS)
		printf("%zu\n", pos);
	return pos != EPOS;
}

static enum CmdStatus cmd_print(Text *txt, const char *cmd) {
	size_t size = text_size(txt), rem = size;
	for (Iterator it = text_iterator_get(txt, 0);
	     rem > 0 && text_iterator_valid(&it);
	     text_iterator_next(&it)) {
		size_t prem = it.end - it.text;
		if (prem > rem)
			prem = rem;
		if (fwrite(it.text, prem, 1, stdout) != 1)
			return CMD_ERR;
		rem -= prem;
	}
	return rem == 0;
}

typedef size_t (*Motion)(Text *, size_t);

static const Motion motions[] = {
	text_line_prev, text_line_begin, text_line_start, text_line_finish, text_line_end,
	text_line_next, text_line_char_next, text_line_char_prev, text_line_blank_next,
	text_line_blank_prev, text_line_up, text_line_down,
	text_char_next, text_char_prev,
	text_longword_end_next, text_longword_end_prev, text_longword_start_next, text_longword_start_prev,
	text_word_end_next, text_word_end_prev, text_word_start_next, text_word_start_prev,
	text_sentence_next, text_sentence_prev, text_paragraph_next, text_paragraph_prev,
	text_lineno_by_pos,
};

static enum CmdStatus cmd_motion(Text *txt, const char *cmd) {
	size_t idx, pos;
	if (sscanf(cmd, "%zu %zu", &idx, &pos) != 2)
		return CMD_ERR;
	if (idx >= LENGTH(motions))
		return CMD_ERR;
	pos = MIN(pos, text_size(txt));
	size_t r = motions[idx](txt, pos);
	printf("%zu\n", r);
	return CMD_OK;
}

typedef Filerange (*Object)(Text *, size_t);

static const Object objects[] = {
	text_object_entire, text_object_word, text_object_word_outer, text_object_longword,
	text_object_longword_outer, text_object_line, text_object_line_inner, text_object_sentence,
	text_object_paragraph, text_object_paragraph_outer, text_object_square_bracket,
	text_object_curly_bracket, text_object_angle_bracket, text_object_parenthesis,
	text_object_quote, text_object_single_quote, text_object_backtick, text_object_indentation,
};

static enum CmdStatus cmd_object(Text *txt, const char *cmd) {
	size_t idx, pos;
	if (sscanf(cmd, "%zu %zu", &idx, &pos) != 2)
		return CMD_ERR;
	if (idx >= LENGTH(objects))
		return CMD_ERR;
	pos = MIN(pos, text_size(txt));
	Filerange r = objects[idx](txt, pos);
	printf("%zu-%zu\n", r.start, r.end);
	return CMD_OK;
}

static enum CmdStatus cmd_find(Text *txt, const char *cmd) {
	char data[BUFSIZ];
	size_t pos;
	if (sscanf(cmd, "%zu %s\n", &pos, data) != 2)
		return CMD_ERR;
	size_t r = text_find_next(txt, pos, data);
	r = text_find_prev(txt, r == EPOS ? pos : r, data);
	return r != EPOS;
}

static enum CmdStatus cmd_quit(Text *txt, const char *cmd) {
	return CMD_QUIT;
}

static const Cmd commands[] = {
	['-'] = cmd_earlier,
	['+'] = cmd_later,
	['?'] = cmd_mark_get,
	['='] = cmd_mark_set,
	['#'] = cmd_size,
	['d'] = cmd_delete,
	['f'] = cmd_find,
	['i'] = cmd_insert,
	['m'] = cmd_motion,
	['o'] = cmd_object,
	['p'] = cmd_print,
	['q'] = cmd_quit,
	['r'] = cmd_redo,
	['s'] = cmd_snapshot,
	['u'] = cmd_undo,
};

static int repl(FILE *input) {
	Text *txt = vis_text_load(NULL, NULL, TEXT_LOAD_AUTO);
	if (!txt)
		return 1;
	mark = EMARK;

	char line[BUFSIZ];
	while (fgets(line, sizeof(line), input)) {
		if (line[0] == '\n')
			continue;
		size_t idx = (unsigned char)line[0];
		if (idx < LENGTH(commands) && commands[idx]) {
			if (commands[idx](txt, line + 1) == CMD_QUIT)
				break;
		}
	}

	text_free(txt);
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
