/*
 * FileSearch - Path Operations Header
 * 
 * Path CRUD, directory scanning with depth control, and path info display.
 */

#ifndef FILESEARCH_PATHS_H
#define FILESEARCH_PATHS_H

#include "utils.h"

/* Path ID lookup */
int get_path_id(const char *path);

/* Path CRUD operations */
int add_path_to_db(const char *path, const char *name, int is_directory, 
                   long long size, const char *parent_path);
int remove_path_from_db(const char *path);

/* Directory scanning 
 * max_depth: -1 = unlimited, 0 = directory only (no contents), 1+ = levels to recurse
 */
int scan_directory(const char *dir_path, int *file_count, int *dir_count, 
                   int current_depth, int max_depth);
void add_directory(const char *path, int max_depth);

/* Path information display */
void show_path_info(const char *path);

#endif /* FILESEARCH_PATHS_H */
