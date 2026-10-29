/*
 * libFuzzer harness for vis-digraph: the real program (vis-digraph.c, included so its static
 * digraph table and lookup() are exercised as built) with main() renamed. The `-` mode reads one
 * byte at a time from a tty; here the fuzz input stands in for that byte stream. The argv mode
 * looks up each NUL-separated word of the input.
 */
#include <stdint.h>
#define main vis_digraph_main
#include "../../vis-digraph.c"
#undef main

static void stream_mode(const uint8_t *data, size_t len) {
	char name[MAX_DIGRAPH_LENGTH + 1] = "";
	for (size_t i = 0; i < sizeof(name) - 1; i++) {
		ssize_t r = i < len ? 1 : 0;
		if (r)
			name[i] = (char)data[i];
		wchar_t rune;
		int l = lookup(name, &rune);
		if (l == 0 || (l == -1 && r == 0))
			return;
		if (l == 1)
			return;
	}
}

static void argv_mode(const uint8_t *data, size_t len) {
	char *copy = malloc(len + 1);
	if (!copy)
		return;
	memcpy(copy, data, len);
	copy[len] = '\0';
	const char *end = copy + len;
	for (const char *word = copy; word <= end; word += strlen(word) + 1) {
		wchar_t rune;
		if (lookup(word, &rune) != 1)
			break;
	}
	free(copy);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len) {
	if (len == 0)
		return -1;
	stream_mode(data, len);
	argv_mode(data, len);
	return 0;
}
