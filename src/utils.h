/*
 * FileSearch - Utility Functions Header
 * 
 * Common utilities, string functions, path handling, and Levenshtein distance.
 */

#ifndef FILESEARCH_UTILS_H
#define FILESEARCH_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <sys/stat.h>
#include <dirent.h>
#include "sqlite3.h"

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define PATH_SEPARATOR '\\'
    #define PATH_SEPARATOR_STR "\\"
    
    /* Wide character conversion for Unicode path support */
    wchar_t *utf8_to_wide(const char *utf8_str);
    char *wide_to_utf8(const wchar_t *wide_str);
#else
    #include <unistd.h>
    #include <pwd.h>
    #define PATH_SEPARATOR '/'
    #define PATH_SEPARATOR_STR "/"
#endif

/* Buffer sizes */
#define MAX_PATH_LENGTH 4096
#define MAX_INPUT_LENGTH 512
#define MAX_TAG_LENGTH 256

/* Application constants */
#define DB_FILENAME "filesearch.db"
#define APP_DIRNAME ".filesearch"

/* Default settings */
#define DEFAULT_SCHEMA_VERSION 2
#define DEFAULT_APP_VERSION 2
#define DEFAULT_SIMILARITY_THRESHOLD 3
#define DEFAULT_MAX_RESULTS 20
#define DEFAULT_FUZZY_DISTANCE 3
#define DEFAULT_SCAN_DEPTH -1  /* -1 = unlimited */

/* Recursion limit */
#define MAX_RECURSION_DEPTH 100

/* UTF-8 and locale support */
void init_utf8_support(void);

/* String utilities */
void trim_whitespace(char *str);
void str_to_lower(char *str);
int min3(int a, int b, int c);

/* User interaction */
int get_confirmation(const char *prompt);

/* Path utilities */
int get_home_directory(char *buffer, size_t size);
int get_default_db_path(char *buffer, size_t size);
int directory_exists(const char *path);
int file_exists(const char *path);
void get_directory_from_path(const char *filepath, char *dir_buffer, size_t size);
const char *get_filename_from_path(const char *path);

/* Argument parsing */
void parse_two_args(const char *input, char *arg1, size_t size1, char *arg2, size_t size2);

/* UTF-8 line input (cross-platform) */
char *read_utf8_line(char *buffer, size_t size, FILE *stream);

/* Levenshtein distance */
int levenshtein(const char *s1, const char *s2);
void sqlite_levenshtein(sqlite3_context *ctx, int argc, sqlite3_value **argv);
int is_substring_match(const char *s1, const char *s2);

#endif /* FILESEARCH_UTILS_H */
