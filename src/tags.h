/*
 * FileSearch - Tag Operations Header
 * 
 * Tag CRUD, path-tag associations, and tag searching.
 */

#ifndef FILESEARCH_TAGS_H
#define FILESEARCH_TAGS_H

#include "utils.h"

/* Tag ID lookup */
int get_tag_id(const char *name);

/* Tag CRUD operations */
int create_tag(const char *name);
int delete_tag(const char *name);
int rename_tag(const char *old_name, const char *new_name);
int prune_unused_tags(void);

/* Tag similarity checking */
int find_similar_tags(const char *new_tag, char *similar_name, size_t name_size, 
                      int *similar_distance, int *is_substring);
int get_or_create_tag_with_check(const char *tag_name);

/* Path-tag associations */
int tag_path(const char *path, const char *tag_name);
int untag_path(const char *path, const char *tag_name);

/* Tag listing */
void list_all_tags(void);
void list_path_tags(const char *path);

/* Tag searching */
void search_tags_all(const char *query);

/* Tag usage count */
int get_tag_usage_count(const char *name);

#endif /* FILESEARCH_TAGS_H */
