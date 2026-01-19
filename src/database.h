/*
 * FileSearch - Database Operations Header
 * 
 * Database initialization, settings, and schema management.
 */

#ifndef FILESEARCH_DATABASE_H
#define FILESEARCH_DATABASE_H

#include "utils.h"

/* Global database handle */
extern sqlite3 *db;

/* Database initialization and cleanup */
int init_database(const char *db_path);
void close_database(void);

/* Settings operations */
int get_int_setting(const char *key, int default_value);
int set_int_setting(const char *key, int value);
char *get_string_setting(const char *key, char *buffer, size_t size, const char *default_value);
int set_string_setting(const char *key, const char *value);
void show_all_settings(void);
void cmd_get_setting(const char *key);
void cmd_set_setting(const char *key, const char *value);

/* Schema management */
int table_exists(const char *table_name);
int create_schema(void);
int insert_default_settings(void);
int insert_default_categories(void);

/* Statistics */
void show_stats(void);

#endif /* FILESEARCH_DATABASE_H */
