
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <unistd.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

static const char EDITOR [] = "nvim";

static const char FILEPATH_PREFIX [] = "/tmp/_blkmv";
static const char FILEPATH_POSTFIX [] = ".txt";

static const char HELP [] =
"blkmv v0.1 by cyman\n\n"
"-h     show Hidden files\n"
"-m     *Make new directories\n"
"-e     *remove Empty directoties\n"
"-R     Recursive\n"
"-f     show Full paths\n\n"
"* you can still create and remove directories without\n"
"  the corresponding option, but you will be prompted to\n"
"  assure this was intended\n\n"
"if you did not intend to see this message you may have forgotten to\n"
"specify any paths or files or you may have used an unknown option\n";

enum {
	ARG_HIDDEN = 0x1,
	ARG_MKDIR  = 0x2,
	ARG_EMPTY  = 0x4,
	ARG_RECUR  = 0x8,
	ARG_FULL   = 0x10,
} arg_mask = 0;

typedef struct Filename {
	char name [256]; // apperantly this is the largest allowed filename
} Filename;

#define sarrlen(arr) (sizeof(arr)/sizeof(*arr))

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

	return (int)*a - (int)*b;
}

static void
paths_join(char * dest, int num_paths, const char * paths []) {
	int dest_index = 0;
	for (int path_index=0; path_index < num_paths; ++path_index) {
		const char * s = paths[path_index];
		while (*s != '\0') {
			dest[dest_index] = *s;
			dest_index++, s++;
		}
		if (path_index != num_paths-1 && dest[dest_index-1] != '/')
			dest[dest_index++] = '/';
	}
	dest[dest_index] = '\0';
}

// no i am not proud of the pointer to a pointer to a pointer
static int
find_recursive(const char * dir_name, int ** file_list, char ** file_list_buffer) {
	DIR * directory = opendir(dir_name);
	struct dirent * entry;
	if (!directory) {
		fprintf(stderr, "could not open directory \"%s\"\n", dir_name);
		return -1;
	}

	while ((entry = readdir(directory)) != NULL) {
		if (entry->d_type == DT_REG) { // regular file
			if (entry->d_name[0] != '.' || (arg_mask & ARG_HIDDEN)) {
				char new_path [PATH_MAX];
				if (strcmp(dir_name, ".") != 0) {
					const char * to_join [] = {dir_name, entry->d_name};
					paths_join(new_path, sarrlen(to_join), to_join);
				} else {
					strcpy(new_path, entry->d_name);
				}

				size_t filename_len = strlen(new_path);
				size_t new_filename_idx = arraddnindex(*file_list_buffer, filename_len + 1);
				strncpy(*file_list_buffer + new_filename_idx, new_path, filename_len);
				(*file_list_buffer)[new_filename_idx + filename_len] = '\0';
				arrput(*file_list, (int)new_filename_idx);
			}
		} else if (entry->d_type == DT_DIR && (arg_mask & ARG_RECUR)) { // directory
			if (entry->d_name[0] != '.' || (arg_mask & ARG_HIDDEN)) {
				char new_path [PATH_MAX];
				if (strcmp(dir_name, ".") != 0) {
					const char * to_join [] = {dir_name, entry->d_name};
					paths_join(new_path, sarrlen(to_join), to_join);
				} else {
					strcpy(new_path, entry->d_name);
				}

				int result = find_recursive(new_path, file_list, file_list_buffer);
				if (result) return result;
			}
		}
	}

	closedir(directory);
	return 0;
}

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
				case 'e': arg_mask |= ARG_EMPTY;  break;
				case 'R': arg_mask |= ARG_RECUR;  break;
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
	int  * og_name_list = NULL;
	char * og_name_buffer = NULL;
	if (chdir(dir_name)) {
		fprintf(stderr, "failed to change working directory\n");
	} else {
		dir_name = ".";
	}

	if (find_recursive(dir_name, &og_name_list, &og_name_buffer)) {
		return -1;
	}

	int count_files = arrlen(og_name_list);

	// create sorted list
	char ** sorted_list = malloc(count_files * sizeof(*sorted_list));
	for (int i=0; i < count_files; ++i) {
		sorted_list[i] = &og_name_buffer[og_name_list[i]];
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
		remove(filename_buf);
		return -1;
	}
	fclose(file);
	buffer[filesize] = '\0';

	char ** new_list = malloc( count_files * sizeof(*new_list) );
	{
		int count_new = 0;
		char * p = buffer;
		new_list[count_new] = p;
		while (*p != '\0') {
			if (*p == '\n') {
				*p = '\0'; // replace newline with null
				count_new++;
				if (count_new < count_files)
					new_list[count_new] = p+1;
			}
			p++;
		}

		if (count_new != count_files) {
			fprintf(stderr, "line count has been changed, no action can be taken\n");
			remove(filename_buf);
			return -1;
		}
	}

	for (int i=0; i < count_files; ++i) {
		int same = strcmp(sorted_list[i], new_list[i]) == 0;
		if (!same) {
			if (new_list[i][0] == '#') {
				remove(sorted_list[i]);
				printf("rm %s\n", sorted_list[i]);
			} else {
				rename(sorted_list[i], new_list[i]);
				printf("%s -> %s\n", sorted_list[i], new_list[i]);
			}
		}
	}

	remove(filename_buf); // delete temporary file

	free(new_list);
	free(buffer);
	free(sorted_list);
	arrfree(og_name_list);
	arrfree(og_name_buffer);

	return 0;
}

