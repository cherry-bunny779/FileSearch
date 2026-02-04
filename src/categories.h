/*
 * FileSearch - Category Operations Header
 * 
 * Category CRUD and path-category associations.
 */

#ifndef FILESEARCH_CATEGORIES_H
#define FILESEARCH_CATEGORIES_H

#include "utils.h"

/* Category ID lookup */
int get_category_id(const char *name);

/* Category CRUD operations */
int create_category(const char *name);
int delete_category(const char *name);
int rename_category(const char *old_name, const char *new_name);

/* Path-category associations */
int categorize_path(const char *path, const char *category_name);
int uncategorize_path(const char *path, const char *category_name);

/* Internal categorization by ID (silent, no output) */
int categorize_path_by_id(int path_id, int category_id);
int uncategorize_path_by_id(int path_id, int category_id);
int assign_uncategorized(int path_id);
int remove_uncategorized(int path_id);
int is_categorized(int path_id);

/* Category listing */
void list_all_categories(void);
void list_path_categories(const char *path);

/* Category root management */
int add_category_root(const char *category_name, const char *root_path);
int remove_category_root(const char *category_name, const char *root_path);
void list_all_roots(void);
void list_category_roots(const char *category_name);
int get_category_roots(const char *category_name, char roots[][MAX_PATH_LENGTH], int max_roots);

#endif /* FILESEARCH_CATEGORIES_H */
