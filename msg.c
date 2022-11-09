#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define SV_IMPLEMENTATION
#include "sv.h"

static char const* program_name;
static char const* manpage_path = "index.1";

typedef struct command
{
	enum {
		Text,
		Link,
	} type;
	String_View value;
} Command;

typedef struct section
{
	String_View name;

	Command *commands;
	size_t commands_count;
	size_t commands_capacity;
} Section;

#define Title_Fields 5

typedef struct page
{
	char const* path;
	String_View title[Title_Fields];

	Section *sections;
	size_t sections_count;
	size_t sections_capacity;
} Page;


static Page parse_page(char const* path);
static String_View read_entire_file(char const* filename);
static void ensure_enough_space(void **mem, size_t element_size, size_t desired_count, size_t *capacity);
static void print_page_to(Page const* page, FILE *out);
static void summary(Page const* page);
static void usage();

#define Push(array, field) \
		ensure_enough_space((void**)&(array).field, sizeof((array).field[0]), ++((array).field##_count), &(array).field##_capacity);

#define Back(array, field) \
	(&((array).field[(array).field##_count-1]))

int main(int argc, char **argv)
{
	program_name = *argv;
	assert(program_name);

	for (int i = 1; --argc; ++i) {
		if (argv[i][0] == '-') {
			if (strcmp("-h", argv[i])) {
				usage();
			}
			fprintf(stderr, "error: unrecognized parameter: %s\n", argv[i]);
			return 2;
		}

		manpage_path = argv[i];
		break;
	}

	Page page = parse_page(manpage_path);
	print_page_to(&page, stdout);

	return 0;
}

static Page parse_page(char const* path)
{
	String_View src = read_entire_file(path);
	Page page = {
		.path = path
	};

	while (src.count != 0) {
		String_View line = sv_chop_by_delim(&src, '\n');

		if (sv_starts_with(line, SV(".TH"))) {
			bool escape = false;
			int cursor = 0, start = 0;

			sv_chop_left(&line, 3);
			line = sv_trim_left(line);
			for (int i = start; i < line.count && cursor < Title_Fields; ++i) {
				if ((!escape && line.data[i] == ' ') || i+1 == line.count) {
					page.title[cursor++] = sv_trim_left((String_View) {
						.data  = line.data + start,
						.count = i - start + 1,
					});
					start = i;
					continue;
				}
				if (line.data[i] == '\\') {
					escape = true;
					continue;
				}
				escape = false;
			}
			continue;
		}

		if (sv_starts_with(line, SV(".SH"))) {
			sv_chop_left(&line, 3);
			line = sv_trim_left(line);
			Push(page, sections);
			Back(page, sections)->name = line;
			continue;
		}

		if (sv_starts_with(line, SV(".LN"))) {
			sv_chop_left(&line, 3);
			Section *last = Back(page, sections);
			Push(*last, commands);
			*Back(*last, commands) = (Command) { .type = Link, .value = line };
			continue;
		}

		if (sv_starts_with(line, SV("."))) {
			fprintf(stderr, "%s: warning: unrecognized command: " SV_Fmt "\n", page.path, SV_Arg(line));
			continue;
		}

		if (page.sections_count == 0) {
			fprintf(stderr, "%s: error: trying to add text without specifing section header .SH\n", page.path);
			exit(1);
		}

		Section *last = Back(page, sections);
		Push(*last, commands);
		*Back(*last, commands) = (Command) { .type = Text, .value = line };
	}

	return page;
}

static void print_page_to(Page const* page, FILE *out)
{
	fprintf(out,
		"<!DOCTYPE html>\n"
		"<html>\n"
		"  <head>\n"
		"    <meta charset=\"utf-8\" />\n");

	fprintf(out, "    <title>" SV_Fmt "</title>\n", SV_Arg(page->title[4])); // TODO add escaping resolution
	fprintf(out, "  </head>\n");
	fprintf(out, "  <body>\n");
	fprintf(out, "  </body>\n");
	fprintf(out, "</html>\n");
}

static void summary(Page const* page)
{
	char const *title_names[] = {
		"title", "section", "date", "source", "manual-section"
	};
	for (int i = 0; i < Title_Fields; ++i) {
		printf("%s: " SV_Fmt "\n", title_names[i], SV_Arg(page->title[i]));
	}

	for (int i = 0; i < page->sections_count; ++i) {
		printf("SECTION " SV_Fmt "\n", SV_Arg(page->sections[i].name));

		for (int j = 0; j < page->sections[i].commands_count; ++j) {
			Command *c = &page->sections[i].commands[j];
			printf("  COMMAND(%d) " SV_Fmt "\n", c->type, SV_Arg(c->value));
		}
	}
}

static void usage()
{
	fprintf(stderr,
		"usage: %s [configuration]\n"
		"  where configuration is a path to INI file storing site settings\n",
		program_name);
	exit(1);
}

static String_View read_entire_file(char const* filename)
{
	FILE *f = filename[0] == '-' && filename[1] == '\0'
		? stdin
		: fopen(filename, "r");

	if (!f) {
		fprintf(stderr, "error: while trying to open file '%s': %s", filename, strerror(errno));
		exit(3);
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buffer = calloc(size + 1, 1);

	String_View content = {
		.data  = buffer,
		.count = size,
	};

	do {
		size_t read = fread(buffer, 1, size, f);
		if (read == 0) {
			break;
		}
		size -= read;
		buffer += read;
	} while (size);

	if (size) {
		fprintf(stderr, "error: while trying to read file '%s': %s", filename, strerror(errno));
		fclose(f);
		exit(4);
	}

	fclose(f);
	return content;
}

static void ensure_enough_space(void **mem, size_t element_size, size_t desired_count, size_t *capacity)
{
	if (desired_count < *capacity) {
		return;
	}

	size_t new_capacity = *capacity > 0 ? desired_count * 2 : 8;

	*mem = *mem ? realloc(*mem, new_capacity * element_size) : malloc(new_capacity * element_size);
	assert(*mem);

	void *old_end = *mem + element_size * *capacity;
	memset(old_end, 0, (new_capacity - *capacity) * element_size);
	*capacity = new_capacity;
}
