/*
 * FileSearch - Search Functions Header
 * 
 * Path searching and structured search with filters.
 */

#ifndef FILESEARCH_SEARCH_H
#define FILESEARCH_SEARCH_H

#include "utils.h"

/* Path search by name (searches the 'name' column - filename only) */
void search_paths_exact(const char *query);
void search_paths_prefix(const char *query);
void search_paths_substring(const char *query);
void search_paths_fuzzy(const char *query, int max_distance);
void search_paths_all(const char *query);

/* Path search by full path (searches the 'path' column - full path) */
void search_fullpath_exact(const char *query);
void search_fullpath_prefix(const char *query);
void search_fullpath_substring(const char *query);
void search_fullpath_fuzzy(const char *query, int max_distance);
void search_fullpath_all(const char *query);

/* Control result storage for batch operations */
void set_store_results(int enabled);

/* Structured search filters */
typedef struct {
    char category[MAX_TAG_LENGTH];     /* Category filter (exact) */
    char tag[MAX_TAG_LENGTH];          /* Tag filter */
    char name[MAX_TAG_LENGTH];         /* Name filter (substring) */
    int tag_fuzzy;                     /* Use fuzzy matching for tag */
    int tag_fuzzy_distance;            /* Max distance for fuzzy tag match */
} SearchFilters;

/* Structured search with filters */
void structured_search(const SearchFilters *filters);

/* Parse find command arguments */
void parse_find_args(const char *args, SearchFilters *filters);

#endif /* FILESEARCH_SEARCH_H */
