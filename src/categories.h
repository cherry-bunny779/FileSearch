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

/* Category listing */
void list_all_categories(void);
void list_path_categories(const char *path);

#endif /* FILESEARCH_CATEGORIES_H */
