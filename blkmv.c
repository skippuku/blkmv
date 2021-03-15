
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <unistd.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

static const char EDITOR [] = "nvim";

static const char FILEPATH_PREFIX [] = "/tmp/_bulkmv";
static const char FILEPATH_POSTFIX [] = ".txt";

static const char HELP [] =
"blkmv v0.1 by cyman\n\n"
"-h     show Hidden files\n"
"-m     *Make new directories\n"
"-r     Recursive\n"
"-f     show Full paths\n\n"
"* you can still create directories and delete files without\n"
"  the corresponding option, but you will be prompted to\n"
"  assure this was intended\n\n"
"if you did not intend to see this message you may have forgotten to\n"
"specify any paths or files or you may have used an unknown option\n";

enum {
	ARG_HIDDEN = 0x1,
	ARG_MKDIR  = 0x2,
	ARG_DELETE = 0x4,
	ARG_RECUR  = 0x8,
	ARG_FULL   = 0x10,
} arg_mask = 0;

typedef struct Filename {
	char name [256]; // apperantly this is the largest allowed filename
} Filename;

#ifdef __GNUC__
#define UNLIKELY(x) __builtin_expect((x), 0)
#else
#define UNLIKELY(x) x
#endif

static int
sort_function(const void * voida, const void * voidb) {
	const char *a = *(const char**)voida, *b = *(const char**)voidb;
	while (*a != '\0' && *b != '\0') {
		if (UNLIKELY(*a <= '9' && *a >= '0' && *b <= '9' && *b >= '0')) {
			char *a_num_end, *b_num_end;
			long a_num = strtol(a, &a_num_end, 10);
			long b_num = strtol(b, &b_num_end, 10);
			long diff = a_num - b_num;
			if (diff != 0)
				return diff;
			else
				a = a_num_end, b = b_num_end;
		} else {
			int diff = (int)*a - (int)*b;
			if (diff == 0)
				a++, b++;
			else
				return diff;
		}
	}
}

static int
count_lines(const char * p) {
	int result = 0;
	while (*p != '\0') {
		if (*p == '\n') result++;
		p++;
	}

	return result;
}

static int
compare_str_to_line(const char * s, char ** line_loc) {
	char * l = *line_loc;
	while (*s == *l) s++, l++;
	if (*s == '\0' && *l == '\n') {
		*line_loc = l+1;
		return 1;
	} else {
		while (*l++ != '\n');
		*line_loc = l;
		return 0;
	}
}

static void
path_join(char * dest, const char * a, const char * b) {
	int di = 0, bi = 0;
	while (a[di] != '\0') {
		dest[di] = a[di];
		di++;
	}
	if (dest[di-1] != '/') dest[di++] = '/';

	while (b[bi] != '\0') {
		dest[di+bi] = b[bi];
		bi++;
	}
	dest[di+bi+1] = '\0';
}

#if 0
int
main() {
	static const char d [] = "gaming";
	static const char d2 [] = "extra/";

	static const char f [] = "file.txt";

	char * joined1 = malloc(PATH_MAX);
	char * joined2 = malloc(PATH_MAX);

	path_join(joined1, d, f);
	path_join(joined2, d2, f);

	printf("%s\n%s\n", joined1, joined2);

	free(joined2);
	free(joined1);

	return 0;
}
#endif

int
main(int argc, char ** args) {
	char * dir_name = NULL;

	// parse arguments
	for (int i=1; i < argc; ++i) {
		if (args[i][0] == '-') {
			int len = strlen(args[i]);
			for (int o=1; o < len; ++o) {
				switch (args[i][o]) {
				case 'h': arg_mask |= ARG_HIDDEN; break;
				case 'm': arg_mask |= ARG_MKDIR;  break;
				case 'd': arg_mask |= ARG_DELETE; break;
				case 'r': arg_mask |= ARG_RECUR;  break;
				case 'f': arg_mask |= ARG_FULL;   break;
				default:
					fprintf(stderr, "unknown option '%c'", args[i][o]);
					break;
				}
			}
		} else {
			// TODO: make list of files and directories
			dir_name = args[i];
		}
	}

	if (dir_name == NULL) {
		fputs(HELP, stderr);
		return 0;
	}

	// create a temporary file so it can be opened in the editor
	// the program makes sure to create a unique file
	// in case there are two instances running at once
	char filename_buf [64];
	int mid_num = 0;
	int collision = 1;
	while (collision) {
		snprintf(filename_buf, sizeof(filename_buf), "%s%i%s", FILEPATH_PREFIX, mid_num, FILEPATH_POSTFIX);
		FILE * file = fopen(filename_buf, "r");
		if (!file) {
			collision = 0;
		} else {
			fclose(file);
			mid_num++;
		}
	}

	// create list of files
	if (chdir(dir_name)) {
		printf("failed to change working directory.\n");
		return -1;
	}
	Filename * og_name_list = NULL;
	DIR * directory = opendir(".");
	struct dirent * entry;
	if (!directory) {
		printf("could not open directory \"%s\"\n", dir_name);
		return -1;
	}
	while ((entry = readdir(directory)) != NULL) {
		if (entry->d_type == DT_REG) { // only list actual files
			if (entry->d_name[0] != '.' || (arg_mask & ARG_HIDDEN)) {
				Filename * this_file = arraddnptr(og_name_list, 1);
				strncpy(this_file->name, entry->d_name, sizeof(this_file->name));
			}
		}
	}
	closedir(directory);

	int count_files = arrlen(og_name_list);

	// create sorted list
	char ** sorted_list = malloc(count_files * sizeof(*sorted_list));
	for (int i=0; i < count_files; ++i) {
		sorted_list[i] = og_name_list[i].name;
	}
	qsort(sorted_list, count_files, sizeof(*sorted_list), sort_function);

	// print all the names to the file
	FILE * file = fopen(filename_buf, "w");
	for (int i=0; i < count_files; ++i) {
		fprintf(file, "%s\n", sorted_list[i]);
	}
	fclose(file);

	// open file in editor
	char command [128];
	snprintf(command, sizeof(command), "%s %s", EDITOR, filename_buf);
	int _result = system(command);

	// load edited file into buffer
	file = fopen(filename_buf, "r");
	fseek(file, 0l, SEEK_END);
	size_t filesize = ftell(file);
	fseek(file, 0l, SEEK_SET);
	char * buffer = malloc(filesize+1);
	if (!fread(buffer, filesize, 1, file)) {
		fclose(file);
		fprintf(stderr, "failed to read temporary file.\n");
		return -1;
	}
	fclose(file);
	buffer[filesize] = '\0';

	// make sure there are the same number of lines
	if (count_files != count_lines(buffer)) {
		fputs("line count does not match, cannot parse\n", stderr);
		return -1;
	}

	char * line = buffer;
	for (int i=0; i < count_files; ++i) {
		char * next_line = line;
		int same = compare_str_to_line(sorted_list[i], &next_line);
		if (!same) {
			if (line[0] == '#') {
				remove(sorted_list[i]);
				printf("rm %s\n", sorted_list[i]);
			} else {
				*(next_line-1) = '\0'; // replace newline with null
				rename(sorted_list[i], line);
				printf("%s -> %s\n", sorted_list[i], line);
			}
		}
		line = next_line;
	}

	// delete temporary file
	remove(filename_buf);

	free(buffer);
	free(sorted_list);
	arrfree(og_name_list);

	return 0;
}

