/*
Copyright (C) 2021 cyman

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <sys/syslimits.h>
#else
#include <linux/limits.h>
#endif

#define STB_DS_IMPLEMENTATION
#include "ext/stb_ds.h"

static const char DEFAULT_EDITOR [] = "$EDITOR";

static const char FILEPATH_PREFIX [] = "/tmp/";
static const char FILEPATH_POSTFIX [] = ".blkmv";

static const char HELP [] =
"blkmv v1.4 Copyright (C) 2021 cyman\n\n"
"usage: blkmv [OPTIONS] DIRECTORY\n"
"-R     [R]ecursive\n"
"-h     show [h]idden files\n"
"-f     show [f]ull paths\n"
"-q     [q]uiet (no output)\n"
"-D     [D]irectory mode\n"
;

static const char HELP_EXTRA [] =
"\n"
"--order <name/date/size/type[:name/date/size]>\n"
"    Order files by name (the default), modification date\n"
"    (newest first), size (smallest first), or file type.\n"
"    The type option allows another optional option\n"
"    specified after a ':' for specifying ordering used\n"
"    within file types (default is name).\n"
"--reverse\n"
"    Reverses file ordering.\n"
;

static enum {
	ARG_HIDDEN = 0x01,
	ARG_RECUR  = 0x08,
	ARG_FULL   = 0x10,
	ARG_DMODE  = 0x20,
	ARG_QUIET  = 0x40,
} arg_mask = 0;

#define LENGTH(x) (sizeof(x)/sizeof(*(x)))

typedef struct FileInfo {
	const char * name;
	int nslashes;
	union {
		size_t size;
		time_t mod_time;
	};
} FileInfo;

#define STRING_BUCKET_CAPACITY 65536 // 64 KiB
typedef struct StringBucket {
	unsigned int length;
	char * data;
} StringBucket;

StringBucket
StringBucket_create() {
	StringBucket result;

	result.length = 0;
	result.data = malloc(STRING_BUCKET_CAPACITY);

	return result;
}

typedef int (*sort_function_t)(const FileInfo*, const FileInfo*);

static int
count_slashes(const char * str) {
	int result = 0;
	while (*str != '\0') {
		if (*str == '/')
			result++;
		str++;
	}
	return result;
}

static int gSortDirection = 1;
static sort_function_t sort_function_child;
static sort_function_t sort_function_type_next;

static int
sort_function_prime(const void * voida, const void * voidb) {
	const FileInfo * info_a = (FileInfo*)voida;
	const FileInfo * info_b = (FileInfo*)voidb;

	int slash_diff = info_b->nslashes - info_a->nslashes;
	if (slash_diff) return slash_diff;

	return sort_function_child(info_a, info_b);
}

static int
sort_function_name(const FileInfo * info_a, const FileInfo * info_b) {
	const char * a = info_a->name;
	const char * b = info_b->name;
	while (*a != '\0' && *b != '\0') {
		if (*a <= '9' && *a >= '0' && *b <= '9' && *b >= '0') {
			char *a_num_end, *b_num_end;
			long a_num = strtol(a, &a_num_end, 10);
			long b_num = strtol(b, &b_num_end, 10);
			long diff = a_num - b_num;
			if (diff != 0)
				return diff * gSortDirection;
			else
				a = a_num_end, b = b_num_end;
		} else {
			int diff = (int)*a - (int)*b;
			if (diff == 0)
				a++, b++;
			else
				return diff * gSortDirection;
		}
	}

	return ((int)*a - (int)*b) * gSortDirection;
}

static int
sort_function_type(const FileInfo * info_a, const FileInfo * info_b) {
	const char * a = strrchr(info_a->name, '.');
	const char * b = strrchr(info_b->name, '.');
	if (a && b) {
		while (*a != '\0' && *b != '\0') {
			int diff = (int)*a - (int)*b;
			if (diff == 0)
				a++, b++;
			else
				return diff * gSortDirection;
		}
	}

	return sort_function_type_next(info_a, info_b);
}

static int
sort_function_size(const FileInfo * info_a, const FileInfo * info_b) {
	if (info_a->size < info_b->size)
		return gSortDirection;
	else if (info_a->size > info_b->size)
		return -gSortDirection;
	else
		return 0;
}

static int
sort_function_mod(const FileInfo * info_a, const FileInfo * info_b) {
	if (info_a->mod_time < info_b->mod_time)
		return gSortDirection;
	else if (info_a->mod_time > info_b->mod_time)
		return -gSortDirection;
	else
		return 0;
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

static void
make_new_path(const char * dir_name, const char * d_name, char * new_path) {
	if (strcmp(dir_name, ".") != 0) {
		const char * to_join [] = {dir_name, d_name};
		paths_join(new_path, LENGTH(to_join), to_join);
	} else {
		strcpy(new_path, d_name);
	}
}

static int
find_recursive(const char * dir_name, char *** file_list, StringBucket ** file_list_buffer) {
	DIR * directory = opendir(dir_name);
	struct dirent * entry;
	if (!directory) {
		fprintf(stderr, "could not open directory \"%s\"\n", dir_name);
		return -1;
	}

	int open_type = (arg_mask & ARG_DMODE) ? DT_DIR : DT_REG;
	while ((entry = readdir(directory)) != NULL) {
		if (entry->d_type == open_type) {
			if (entry->d_name[0] != '.' || (arg_mask & ARG_HIDDEN)) {
				char new_path [PATH_MAX];
				make_new_path(dir_name, entry->d_name, new_path);

				size_t filename_len = strlen(new_path);
				StringBucket * bucket = &arrlast(*file_list_buffer);
				if (bucket->length + filename_len + 1 > STRING_BUCKET_CAPACITY) {
					arrput(*file_list_buffer, StringBucket_create());
					bucket = &arrlast(*file_list_buffer);
				}
				char * new_filename_loc = &bucket->data[bucket->length];
				strcpy(new_filename_loc, new_path);
				bucket->length += filename_len + 1;

				arrput(*file_list, new_filename_loc);
			}
		}
		if (entry->d_type == DT_DIR && (arg_mask & ARG_RECUR)) {
			if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
				if (entry->d_name[0] != '.' || (arg_mask & ARG_HIDDEN)) {
					char new_path [PATH_MAX];
					make_new_path(dir_name, entry->d_name, new_path);

					int result = find_recursive(new_path, file_list, file_list_buffer);
					if (result) {
						closedir(directory);
						return result;
					}
				}
			}
		}
	}

	closedir(directory);
	return 0;
}

static int
get_dir_name(char * ret_dir_name, const char * path) {
	char * last_slash = strrchr(path, '/');
	if (last_slash == NULL)
		return 0;
	size_t dir_name_size = last_slash - path;
	strncpy(ret_dir_name, path, dir_name_size);
	ret_dir_name[dir_name_size] = '\0';
	return dir_name_size;
}

#define rprintf(...) if(!(arg_mask&ARG_QUIET))printf(__VA_ARGS__)

void
get_bash_path(char * dest, const char * str) {
	*dest++ = '"';
	while (*str) {
		if (*str == '"' || *str == '`' || *str == '\\' || *str == '$')
			*dest++ = '\\';
		*dest++ = *str;
		str++;
	}
	*dest++ = '"';
	*dest = '\0';
}

static int
remove_empty_recursive(const char * dir_path) {
	char dir_name [PATH_MAX];
	int dir_name_size = get_dir_name(dir_name, dir_path);
	if (dir_name_size == 0) return 0;

	DIR * dir = opendir(dir_name);
	struct dirent * entry;
	if (!dir) {
		fprintf(stderr, "failed to open %s\n", dir_name);
		return -1;
	}

	int file_count = 0;
	while((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
			file_count++;
	}
	closedir(dir);

	if (file_count == 0) {
		remove(dir_name);
		char arg [PATH_MAX];
		get_bash_path(arg, dir_name);
		rprintf("rm -r %s\n", arg);
		return remove_empty_recursive(dir_name);
	} else {
		return 0;
	}
}

sort_function_t
get_sort_function_from_string(const char * str) {
	if (strcmp(str, "name") == 0) {
		return sort_function_name;
	} else if (strcmp(str, "size") == 0) {
		return sort_function_size;
	} else if (strcmp(str, "date") == 0) {
		return sort_function_mod;
	} else {
		fprintf(stderr, "unknown sort order \"%s\".\n", str);
		return NULL;
	}
}

int
do_move(const char * old_name, const char * new_name) {
	int same = strcmp(old_name, new_name) == 0;
	if (same) return 0;

	if (new_name[0] == '#') {
		int error = remove(old_name);
		char arg [PATH_MAX];
		get_bash_path(arg, old_name);
		rprintf("rm %s\n", arg);
		if (error) rprintf(" # FAILED!");
	} else {
		char dir_name [PATH_MAX];
		int dir_name_size = get_dir_name(dir_name, new_name);
		if (dir_name_size) {
			struct stat dir_stat;
			if (!(stat(dir_name, &dir_stat) == 0 && dir_stat.st_mode & S_IFDIR)) {
				char * cmd_buffer = malloc(dir_name_size + 12);
				char arg [PATH_MAX];
				get_bash_path(arg, dir_name);
				sprintf(cmd_buffer, "mkdir -p %s", arg);
				int sys_result = system(cmd_buffer);
				if (sys_result != 0) {
					fprintf(stderr, "failed to create directory '%s'\n", dir_name);
					return -1;
				}
				rprintf("%s\n", cmd_buffer);
				free(cmd_buffer);
			}
		}
		int error = rename(old_name, new_name);
		char a1 [PATH_MAX], a2 [PATH_MAX];
		get_bash_path(a1, old_name);
		get_bash_path(a2, new_name);
		rprintf("mv %s %s\n", a1, a2);
		if (error) rprintf(" # FAILED!");

		int result = remove_empty_recursive(old_name);
		if (result) return result;
	}
	return 0;
}

int
main(int argc, char ** args) {
	const char * editor = DEFAULT_EDITOR;
	if (editor[0] == '$' && !getenv(editor + 1)) {
		fprintf(stderr, "no environment variable: '%s'\n", editor + 1);
		return 1;
	}
	char * dir_name = NULL;

	// defaults
	sort_function_child = sort_function_name;
	sort_function_type_next = sort_function_name;

	// parse arguments
	for (int i=1; i < argc; ++i) {
		if (args[i][0] == '-') {
			if (args[i][1] == '-') {
				if (strcmp(&args[i][2], "order") == 0) {
					i++;
					if (strncmp(args[i], "type", 4) == 0) {
						sort_function_child = sort_function_type;
						if (args[i][4] == ':') {
							sort_function_type_next = get_sort_function_from_string(&args[i][5]);
							if (sort_function_type_next == NULL) {
								return 1;
							}
						}
					} else {
						sort_function_child = get_sort_function_from_string(args[i]);
						if (sort_function_child == NULL) {
							return 1;
						}
					}
				} else if (strcmp(&args[i][2], "reverse") == 0) {
					gSortDirection = -1;
				} else if (strcmp(&args[i][2], "help") == 0) {
					fputs(HELP, stderr);
					fputs(HELP_EXTRA, stderr);
					return 1;
				} else {
					fprintf(stderr, "unknown option \"%s\"\n", &args[i][2]);
					return 1;
				}
			} else {
				int len = strlen(args[i]);
				for (int o=1; o < len; ++o) {
					switch (args[i][o]) {
					case 'h': arg_mask |= ARG_HIDDEN; break;
					case 'R': arg_mask |= ARG_RECUR;  break;
					case 'f': arg_mask |= ARG_FULL;   break;
					case 'q': arg_mask |= ARG_QUIET;  break;
					case 'D': arg_mask |= ARG_DMODE;  break;
					default:
						fprintf(stderr, "unknown option '%c'\n", args[i][o]);
						return 1;
					}
				}
			}
		} else {
			if (dir_name == NULL) {
				dir_name = args[i];
			} else {
				fprintf(stderr, "cannot process more than one directory\n");
				return 1;
			}
		}
	}

	if (dir_name == NULL) {
		fputs("no directory was passed\n", stderr);
		fputs(HELP, stderr);
		fputs("try \"blkmv --help\" for additional information.\n", stderr);
		return 1;
	}

	// create a temporary file so it can be opened in the editor
	// the program makes sure to create a unique file
	// in case there are two instances running at once
	char filename_buf [64];
	int mid_num = 0;
	do {
		snprintf(filename_buf, sizeof(filename_buf), "%s%i%s", FILEPATH_PREFIX, mid_num++, FILEPATH_POSTFIX);
	} while(access(filename_buf, F_OK) == 0);

	// create list of files
	{
		char dir_name_full [PATH_MAX];
		if (!(arg_mask & ARG_FULL)) {
			if (chdir(dir_name)) {
				fprintf(stderr, "failed to change working directory\n");
			} else {
				dir_name = ".";
			}
		} else {
			if (realpath(dir_name, dir_name_full)) {
				dir_name = dir_name_full;
			}
		}
	}
	char        ** og_name_list = NULL;
	StringBucket * og_name_buffer = NULL;
	arrput(og_name_buffer, StringBucket_create());
	if (find_recursive(dir_name, &og_name_list, &og_name_buffer)) {
		return -1;
	}

	int count_files = arrlen(og_name_list);
	if (count_files == 0) {
		fprintf(stderr, "directory is empty.\n");
		return 1;
	}

	// create sorted list
	FileInfo * sorted_list = malloc(count_files * sizeof(*sorted_list));
	for (int i=0; i < count_files; ++i) {
		sort_function_t temp_sort_function;
		if (sort_function_child == sort_function_type) {
			temp_sort_function = sort_function_type_next;
		} else {
			temp_sort_function = sort_function_child;
		}

		FileInfo new;
		new.name = og_name_list[i];
		new.nslashes = count_slashes(new.name);
		if (temp_sort_function == sort_function_size || temp_sort_function == sort_function_mod) {
			struct stat new_stat;
			stat(new.name, &new_stat);
			if (temp_sort_function == sort_function_size) {
				new.size = new_stat.st_size;
			} else if (temp_sort_function == sort_function_mod) {
				new.mod_time = new_stat.st_mtime;
			}
		}
		sorted_list[i] = new;
	}
	qsort(sorted_list, count_files, sizeof(*sorted_list), sort_function_prime);

	// print all the names to the file
	FILE * file = fopen(filename_buf, "w");
	for (int i=0; i < count_files; ++i) {
		fprintf(file, "%s\n", sorted_list[i].name);
	}
	fclose(file);

	// open file in editor
	char command [128];
	snprintf(command, sizeof(command), "%s %s", editor, filename_buf);
	int cmd_result = system(command);
	if (cmd_result) {
		fprintf(stderr, "failed to execute \"%s\"\n", command);
		remove(filename_buf);
		return -1;
	}

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
	remove(filename_buf); // delete temporary file
	buffer[filesize] = '\0';

	// get new names
	char ** new_names = malloc( count_files * sizeof(*new_names) );
	{
		int count_new = 0;
		char * p = buffer;
		new_names[count_new] = p;
		while (*p != '\0') {
			if (*p == '\n') {
				*p = '\0'; // replace newline with null
				count_new++;
				if (count_new < count_files) {
					new_names[count_new] = p+1;
				}
			}
			p++;
		}

		if (count_new != count_files) {
			fprintf(stderr, "line count was changed, no action can be taken\n");
			return -1;
		}
	}

	for (int i=0; i < count_files; i++) {
		int error = do_move(sorted_list[i].name, new_names[i]);
		if (error) return error;
	}

	for (int i=arrlen(og_name_buffer)-1; i >= 0; --i) {
		free(og_name_buffer[i].data);
	}
	free(new_names);
	free(buffer);
	free(sorted_list);
	arrfree(og_name_list);
	arrfree(og_name_buffer);

	return 0;
}
