/*
 * FileSearch - Lightweight Path Management System
 * Version 2: Categories, Tags, and Structured Search
 * 
 * Features:
 * - Persistent SQLite database storage
 * - Add directories/files to database
 * - Categories and tags (many-to-many relationships)
 * - Search by name, tags, categories (exact, prefix, substring, fuzzy)
 * - Structured search with --category, --tag, --name flags
 * - Database-stored settings with schema versioning
 * - Tag similarity warnings (Levenshtein + substring)
 * - Cross-platform support (Windows/macOS/Linux)
 * 
 * Compile:
 *   Linux/macOS: gcc -o filesearch filesearch_v2.c -lsqlite3
 *   Windows:     gcc -o filesearch.exe filesearch_v2.c -lsqlite3
 * 
 * Usage:
 *   filesearch [--db /path/to/database.db]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include "../deps/sqlite3.h"

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define PATH_SEPARATOR '\\'
    #define PATH_SEPARATOR_STR "\\"
#else
    #include <unistd.h>
    #include <pwd.h>
    #define PATH_SEPARATOR '/'
    #define PATH_SEPARATOR_STR "/"
#endif

#define MAX_PATH_LENGTH 4096
#define MAX_INPUT_LENGTH 512
#define MAX_TAG_LENGTH 256
#define DB_FILENAME "filesearch.db"
#define APP_DIRNAME ".filesearch"

/* Default settings (used when creating new database) */
#define DEFAULT_SCHEMA_VERSION 1
#define DEFAULT_APP_VERSION 1
#define DEFAULT_SIMILARITY_THRESHOLD 3
#define DEFAULT_MAX_RESULTS 20
#define DEFAULT_FUZZY_DISTANCE 3

/* ============================================
 * Utility Functions
 * ============================================ */

int min3(int a, int b, int c) {
    int min = a;
    if (b < min) min = b;
    if (c < min) min = c;
    return min;
}

void trim_whitespace(char *str) {
    char *start = str;
    while (*start && isspace(*start)) start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    size_t len = strlen(str);
    while (len > 0 && isspace(str[len - 1])) {
        str[--len] = '\0';
    }
}

void str_to_lower(char *str) {
    for (char *p = str; *p; p++) {
        *p = tolower(*p);
    }
}

int get_confirmation(const char *prompt) {
    char response[16];
    printf("%s (y/n): ", prompt);
    fflush(stdout);
    
    if (!fgets(response, sizeof(response), stdin)) {
        return 0;
    }
    
    trim_whitespace(response);
    return (response[0] == 'y' || response[0] == 'Y');
}

/* ============================================
 * Cross-Platform Path Handling
 * ============================================ */

int get_home_directory(char *buffer, size_t size) {
#ifdef _WIN32
    const char *userprofile = getenv("USERPROFILE");
    if (userprofile && strlen(userprofile) < size) {
        strcpy(buffer, userprofile);
        return 0;
    }
    return -1;
#else
    const char *home = getenv("HOME");
    if (home && strlen(home) < size) {
        strcpy(buffer, home);
        return 0;
    }
    
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir && strlen(pw->pw_dir) < size) {
        strcpy(buffer, pw->pw_dir);
        return 0;
    }
    return -1;
#endif
}

int get_default_db_path(char *buffer, size_t size) {
    char home[MAX_PATH_LENGTH];
    
    if (get_home_directory(home, sizeof(home)) != 0) {
        return -1;
    }
    
    int written = snprintf(buffer, size, "%s%s%s%s%s", 
                           home, PATH_SEPARATOR_STR, 
                           APP_DIRNAME, PATH_SEPARATOR_STR, 
                           DB_FILENAME);
    
    if (written < 0 || (size_t)written >= size) {
        return -1;
    }
    
    return 0;
}

int directory_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return 0;
}

int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

void get_directory_from_path(const char *filepath, char *dir_buffer, size_t size) {
    strncpy(dir_buffer, filepath, size - 1);
    dir_buffer[size - 1] = '\0';
    
    char *last_sep = strrchr(dir_buffer, PATH_SEPARATOR);
    if (last_sep) {
        *last_sep = '\0';
    }
}

const char *get_filename_from_path(const char *path) {
    const char *last_sep = strrchr(path, PATH_SEPARATOR);
#ifdef _WIN32
    const char *last_fwd = strrchr(path, '/');
    if (last_fwd && (!last_sep || last_fwd > last_sep)) {
        last_sep = last_fwd;
    }
#endif
    return last_sep ? last_sep + 1 : path;
}

/* ============================================
 * Levenshtein Distance
 * ============================================ */

int levenshtein(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    if (len1 > len2) {
        const char *temp = s1;
        s1 = s2;
        s2 = temp;
        int t = len1;
        len1 = len2;
        len2 = t;
    }
    
    int *prev = malloc((len1 + 1) * sizeof(int));
    int *curr = malloc((len1 + 1) * sizeof(int));
    
    if (!prev || !curr) {
        free(prev);
        free(curr);
        return -1;
    }
    
    for (int i = 0; i <= len1; i++) {
        prev[i] = i;
    }
    
    for (int j = 1; j <= len2; j++) {
        curr[0] = j;
        
        for (int i = 1; i <= len1; i++) {
            int cost = (tolower(s1[i-1]) == tolower(s2[j-1])) ? 0 : 1;
            curr[i] = min3(prev[i] + 1, curr[i-1] + 1, prev[i-1] + cost);
        }
        
        int *temp = prev;
        prev = curr;
        curr = temp;
    }
    
    int result = prev[len1];
    free(prev);
    free(curr);
    
    return result;
}

void sqlite_levenshtein(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (argc != 2) {
        sqlite3_result_error(ctx, "levenshtein requires 2 arguments", -1);
        return;
    }
    
    const char *s1 = (const char *)sqlite3_value_text(argv[0]);
    const char *s2 = (const char *)sqlite3_value_text(argv[1]);
    
    if (!s1 || !s2) {
        sqlite3_result_null(ctx);
        return;
    }
    
    sqlite3_result_int(ctx, levenshtein(s1, s2));
}

/*
 * Case-insensitive substring check.
 * Returns 1 if needle is in haystack (or vice versa), 0 otherwise.
 */
int is_substring_match(const char *s1, const char *s2) {
    char lower1[MAX_TAG_LENGTH];
    char lower2[MAX_TAG_LENGTH];
    
    strncpy(lower1, s1, sizeof(lower1) - 1);
    lower1[sizeof(lower1) - 1] = '\0';
    strncpy(lower2, s2, sizeof(lower2) - 1);
    lower2[sizeof(lower2) - 1] = '\0';
    
    str_to_lower(lower1);
    str_to_lower(lower2);
    
    return (strstr(lower1, lower2) != NULL || strstr(lower2, lower1) != NULL);
}

/* ============================================
 * Database Globals
 * ============================================ */

sqlite3 *db = NULL;

/* ============================================
 * Settings Operations
 * ============================================ */

int get_int_setting(const char *key, int default_value) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT value FROM settings WHERE key = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return default_value;
    }
    
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    
    int result = default_value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = atoi((const char *)sqlite3_column_text(stmt, 0));
    }
    
    sqlite3_finalize(stmt);
    return result;
}

int set_int_setting(const char *key, int value) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%d", value);
    
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value_str, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

char *get_string_setting(const char *key, char *buffer, size_t size, const char *default_value) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT value FROM settings WHERE key = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        strncpy(buffer, default_value, size - 1);
        buffer[size - 1] = '\0';
        return buffer;
    }
    
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *val = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(buffer, val ? val : default_value, size - 1);
    } else {
        strncpy(buffer, default_value, size - 1);
    }
    buffer[size - 1] = '\0';
    
    sqlite3_finalize(stmt);
    return buffer;
}

int set_string_setting(const char *key, const char *value) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

void show_all_settings() {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT key, value FROM settings ORDER BY key;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    printf("\n[Settings]\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *key = (const char *)sqlite3_column_text(stmt, 0);
        const char *value = (const char *)sqlite3_column_text(stmt, 1);
        printf("  %-25s %s\n", key, value ? value : "(null)");
    }
    printf("\n");
    
    sqlite3_finalize(stmt);
}

void cmd_get_setting(const char *key) {
    char buffer[256];
    get_string_setting(key, buffer, sizeof(buffer), "(not set)");
    printf("%s = %s\n", key, buffer);
}

void cmd_set_setting(const char *key, const char *value) {
    if (set_string_setting(key, value) == 0) {
        printf("Updated: %s = %s\n", key, value);
    } else {
        fprintf(stderr, "Failed to update setting.\n");
    }
}

/* ============================================
 * Database Initialization
 * ============================================ */

int table_exists(const char *table_name) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_STATIC);
    
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    return exists;
}

int create_schema_v1() {
    const char *schema = 
        /* Paths table */
        "CREATE TABLE IF NOT EXISTS paths ("
        "  id INTEGER PRIMARY KEY,"
        "  path TEXT UNIQUE NOT NULL,"
        "  name TEXT NOT NULL,"
        "  is_directory INTEGER NOT NULL,"
        "  size INTEGER,"
        "  parent_path TEXT"
        ");"
        
        /* Categories table */
        "CREATE TABLE IF NOT EXISTS categories ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT UNIQUE NOT NULL"
        ");"
        
        /* Path-Category junction table */
        "CREATE TABLE IF NOT EXISTS path_categories ("
        "  path_id INTEGER NOT NULL,"
        "  category_id INTEGER NOT NULL,"
        "  PRIMARY KEY (path_id, category_id),"
        "  FOREIGN KEY (path_id) REFERENCES paths(id) ON DELETE CASCADE,"
        "  FOREIGN KEY (category_id) REFERENCES categories(id) ON DELETE CASCADE"
        ");"
        
        /* Tags table */
        "CREATE TABLE IF NOT EXISTS tags ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT UNIQUE NOT NULL"
        ");"
        
        /* Path-Tag junction table */
        "CREATE TABLE IF NOT EXISTS path_tags ("
        "  path_id INTEGER NOT NULL,"
        "  tag_id INTEGER NOT NULL,"
        "  PRIMARY KEY (path_id, tag_id),"
        "  FOREIGN KEY (path_id) REFERENCES paths(id) ON DELETE CASCADE,"
        "  FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE"
        ");"
        
        /* Settings table */
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT"
        ");"
        
        /* Indexes */
        "CREATE INDEX IF NOT EXISTS idx_path_name ON paths(name);"
        "CREATE INDEX IF NOT EXISTS idx_path_parent ON paths(parent_path);"
        "CREATE INDEX IF NOT EXISTS idx_path_is_dir ON paths(is_directory);"
        "CREATE INDEX IF NOT EXISTS idx_category_name ON categories(name);"
        "CREATE INDEX IF NOT EXISTS idx_tag_name ON tags(name);"
        "CREATE INDEX IF NOT EXISTS idx_path_categories_path ON path_categories(path_id);"
        "CREATE INDEX IF NOT EXISTS idx_path_categories_cat ON path_categories(category_id);"
        "CREATE INDEX IF NOT EXISTS idx_path_tags_path ON path_tags(path_id);"
        "CREATE INDEX IF NOT EXISTS idx_path_tags_tag ON path_tags(tag_id);";
    
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, schema, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Schema error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    return 0;
}

int insert_default_settings() {
    set_int_setting("schema_version", DEFAULT_SCHEMA_VERSION);
    set_int_setting("app_version", DEFAULT_APP_VERSION);
    set_int_setting("similarity_threshold", DEFAULT_SIMILARITY_THRESHOLD);
    set_int_setting("max_results", DEFAULT_MAX_RESULTS);
    set_int_setting("fuzzy_default_distance", DEFAULT_FUZZY_DISTANCE);
    return 0;
}

int insert_default_categories() {
    const char *categories[] = {"Games", "Music", "Photos", "Documents", "Uncategorized"};
    int count = sizeof(categories) / sizeof(categories[0]);
    
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR IGNORE INTO categories (name) VALUES (?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    for (int i = 0; i < count; i++) {
        sqlite3_bind_text(stmt, 1, categories[i], -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

int init_database(const char *db_path) {
    /* Check if parent directory exists */
    char dir_path[MAX_PATH_LENGTH];
    get_directory_from_path(db_path, dir_path, sizeof(dir_path));
    
    if (!directory_exists(dir_path)) {
        fprintf(stderr, "Error: Directory '%s' does not exist.\n", dir_path);
#ifdef _WIN32
        fprintf(stderr, "Please create it with: mkdir \"%s\"\n", dir_path);
#else
        fprintf(stderr, "Please create it with: mkdir -p %s\n", dir_path);
#endif
        return -1;
    }
    
    int is_new_db = !file_exists(db_path);
    
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database '%s': %s\n", db_path, sqlite3_errmsg(db));
        return -1;
    }
    
    /* Enable foreign keys */
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);
    
    /* Register Levenshtein function */
    rc = sqlite3_create_function(db, "levenshtein", 2, SQLITE_UTF8, NULL,
                                  sqlite_levenshtein, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot register function: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    if (is_new_db) {
        printf("Creating new database: %s\n", db_path);
        
        if (create_schema_v1() != 0) {
            return -1;
        }
        
        insert_default_settings();
        insert_default_categories();
        
        printf("Database initialized with default settings and categories.\n");
    } else {
        printf("Database opened: %s\n", db_path);
        
        /* Check for schema migration */
        int current_version = get_int_setting("schema_version", 0);
        
        if (current_version == 0 && !table_exists("settings")) {
            /* Old database without versioning - needs migration */
            printf("\nDatabase schema update required.\n");
            printf("This will add category support and settings to your existing data.\n");
            printf("Existing paths will be assigned to 'Uncategorized'.\n\n");
            
            if (!get_confirmation("Proceed with migration?")) {
                printf("Migration cancelled. Exiting.\n");
                sqlite3_close(db);
                db = NULL;
                return -1;
            }
            
            /* Perform migration */
            if (create_schema_v1() != 0) {
                return -1;
            }
            
            insert_default_settings();
            insert_default_categories();
            
            /* Assign existing paths to Uncategorized */
            const char *migrate_sql = 
                "INSERT OR IGNORE INTO path_categories (path_id, category_id) "
                "SELECT p.id, c.id FROM paths p, categories c WHERE c.name = 'Uncategorized';";
            
            sqlite3_exec(db, migrate_sql, NULL, NULL, NULL);
            
            printf("Migration complete.\n");
        } else if (current_version < DEFAULT_SCHEMA_VERSION) {
            /* Future migrations would go here */
            printf("Schema is up to date (version %d).\n", current_version);
        }
    }
    
    return 0;
}

/* ============================================
 * Path Operations
 * ============================================ */

int get_path_id(const char *path) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id FROM paths WHERE path = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    
    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return id;
}

int add_path_to_db(const char *path, const char *name, int is_directory, 
                   long long size, const char *parent_path) {
    sqlite3_stmt *stmt;
    const char *sql = 
        "INSERT OR IGNORE INTO paths (path, name, is_directory, size, parent_path) "
        "VALUES (?, ?, ?, ?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, is_directory);
    
    if (size >= 0) {
        sqlite3_bind_int64(stmt, 4, size);
    } else {
        sqlite3_bind_null(stmt, 4);
    }
    
    if (parent_path) {
        sqlite3_bind_text(stmt, 5, parent_path, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 5);
    }
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int remove_path_from_db(const char *path) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        fprintf(stderr, "Path not found in database: %s\n", path);
        return -1;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM paths WHERE id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        printf("Removed: %s\n", path);
        return 0;
    }
    return -1;
}

int scan_directory_recursive(const char *dir_path, int *file_count, int *dir_count, int depth) {
    if (depth > 100) {
        fprintf(stderr, "Warning: Maximum depth reached at %s\n", dir_path);
        return 0;
    }
    
    DIR *dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Cannot open directory: %s\n", dir_path);
        return -1;
    }
    
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    struct stat st;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(full_path, sizeof(full_path), "%s%s%s", 
                 dir_path, PATH_SEPARATOR_STR, entry->d_name);
        
        if (stat(full_path, &st) != 0) {
            fprintf(stderr, "Cannot stat: %s\n", full_path);
            continue;
        }
        
        int is_dir = S_ISDIR(st.st_mode);
        long long size = is_dir ? -1 : (long long)st.st_size;
        
        add_path_to_db(full_path, entry->d_name, is_dir, size, dir_path);
        
        if (is_dir) {
            (*dir_count)++;
            scan_directory_recursive(full_path, file_count, dir_count, depth + 1);
        } else {
            (*file_count)++;
        }
    }
    
    closedir(dir);
    return 0;
}

void add_directory(const char *path) {
    char normalized[MAX_PATH_LENGTH];
    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    
    size_t len = strlen(normalized);
    while (len > 1 && (normalized[len-1] == '/' || normalized[len-1] == '\\')) {
        normalized[--len] = '\0';
    }
    
    if (!directory_exists(normalized)) {
        fprintf(stderr, "Error: '%s' is not a valid directory.\n", normalized);
        return;
    }
    
    printf("Scanning directory: %s\n", normalized);
    
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
    const char *name = get_filename_from_path(normalized);
    add_path_to_db(normalized, name, 1, -1, NULL);
    
    int file_count = 0;
    int dir_count = 1;
    
    scan_directory_recursive(normalized, &file_count, &dir_count, 0);
    
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    
    printf("Added %d files and %d directories.\n\n", file_count, dir_count);
}

/* ============================================
 * Category Operations
 * ============================================ */

int get_category_id(const char *name) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id FROM categories WHERE name = ? COLLATE NOCASE;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    
    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return id;
}

int create_category(const char *name) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO categories (name) VALUES (?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        printf("Created category: %s\n", name);
        return (int)sqlite3_last_insert_rowid(db);
    }
    
    fprintf(stderr, "Failed to create category (may already exist).\n");
    return -1;
}

void list_all_categories() {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM categories ORDER BY name;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    printf("\n[All Categories]\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  %s\n", sqlite3_column_text(stmt, 0));
    }
    printf("\n");
    
    sqlite3_finalize(stmt);
}

void list_path_categories(const char *path) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        fprintf(stderr, "Path not found in database: %s\n", path);
        return;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT c.name FROM categories c "
        "JOIN path_categories pc ON c.id = pc.category_id "
        "WHERE pc.path_id = ? ORDER BY c.name;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    
    printf("\n[Categories for %s]\n", path);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  %s\n", sqlite3_column_text(stmt, 0));
        count++;
    }
    
    if (count == 0) {
        printf("  (no categories)\n");
    }
    printf("\n");
    
    sqlite3_finalize(stmt);
}

int categorize_path(const char *path, const char *category_name) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        fprintf(stderr, "Path not found in database: %s\n", path);
        return -1;
    }
    
    int category_id = get_category_id(category_name);
    if (category_id < 0) {
        fprintf(stderr, "Category not found: %s\n", category_name);
        fprintf(stderr, "Use 'create-category %s' to create it first.\n", category_name);
        return -1;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR IGNORE INTO path_categories (path_id, category_id) VALUES (?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    sqlite3_bind_int(stmt, 2, category_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        printf("Categorized: %s [%s]\n", path, category_name);
        return 0;
    }
    return -1;
}

int uncategorize_path(const char *path, const char *category_name) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        fprintf(stderr, "Path not found in database: %s\n", path);
        return -1;
    }
    
    int category_id = get_category_id(category_name);
    if (category_id < 0) {
        fprintf(stderr, "Category not found: %s\n", category_name);
        return -1;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM path_categories WHERE path_id = ? AND category_id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    sqlite3_bind_int(stmt, 2, category_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        printf("Uncategorized: %s [%s]\n", path, category_name);
        return 0;
    }
    return -1;
}

/* ============================================
 * Tag Operations
 * ============================================ */

int get_tag_id(const char *name) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id FROM tags WHERE name = ? COLLATE NOCASE;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    
    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return id;
}

int create_tag(const char *name) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO tags (name) VALUES (?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        return (int)sqlite3_last_insert_rowid(db);
    }
    return -1;
}

/*
 * Find similar tags using both substring and Levenshtein matching.
 * Returns the number of similar tags found.
 * If found, populates similar_name and similar_distance with the closest match.
 */
int find_similar_tags(const char *new_tag, char *similar_name, size_t name_size, 
                      int *similar_distance, int *is_substring) {
    int threshold = get_int_setting("similarity_threshold", DEFAULT_SIMILARITY_THRESHOLD);
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM tags;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    
    int found_count = 0;
    int best_distance = threshold + 1;
    similar_name[0] = '\0';
    *is_substring = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *existing = (const char *)sqlite3_column_text(stmt, 0);
        
        /* Check substring match first */
        if (is_substring_match(new_tag, existing)) {
            if (found_count == 0 || *is_substring == 0) {
                strncpy(similar_name, existing, name_size - 1);
                similar_name[name_size - 1] = '\0';
                *similar_distance = abs((int)strlen(new_tag) - (int)strlen(existing));
                *is_substring = 1;
                found_count++;
            }
            continue;
        }
        
        /* Check Levenshtein distance */
        int dist = levenshtein(new_tag, existing);
        if (dist > 0 && dist <= threshold && dist < best_distance) {
            strncpy(similar_name, existing, name_size - 1);
            similar_name[name_size - 1] = '\0';
            *similar_distance = dist;
            *is_substring = 0;
            best_distance = dist;
            found_count++;
        }
    }
    
    sqlite3_finalize(stmt);
    return found_count;
}

/*
 * Get or create a tag, with similarity warning.
 * Returns tag ID on success, -1 on failure/cancellation.
 */
int get_or_create_tag_with_check(const char *tag_name) {
    /* Check if exact tag exists */
    int tag_id = get_tag_id(tag_name);
    if (tag_id >= 0) {
        return tag_id;
    }
    
    /* Check for similar existing tags */
    char similar_name[MAX_TAG_LENGTH];
    int similar_distance;
    int is_substring;
    
    if (find_similar_tags(tag_name, similar_name, sizeof(similar_name), 
                          &similar_distance, &is_substring) > 0) {
        if (is_substring) {
            printf("Warning: Similar tag exists: '%s' (substring match)\n", similar_name);
        } else {
            printf("Warning: Similar tag exists: '%s' (distance: %d)\n", 
                   similar_name, similar_distance);
        }
        
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "Create new tag '%s' anyway?", tag_name);
        
        if (!get_confirmation(prompt)) {
            snprintf(prompt, sizeof(prompt), "Use '%s' instead?", similar_name);
            
            if (get_confirmation(prompt)) {
                return get_tag_id(similar_name);
            }
            
            printf("Cancelled.\n");
            return -1;
        }
    }
    
    /* Create new tag */
    tag_id = create_tag(tag_name);
    if (tag_id >= 0) {
        printf("Created tag: %s\n", tag_name);
    }
    return tag_id;
}

int tag_path(const char *path, const char *tag_name) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        fprintf(stderr, "Path not found in database: %s\n", path);
        return -1;
    }
    
    int tag_id = get_or_create_tag_with_check(tag_name);
    if (tag_id < 0) {
        return -1;
    }
    
    /* Check if already tagged */
    sqlite3_stmt *check_stmt;
    const char *check_sql = "SELECT 1 FROM path_tags WHERE path_id = ? AND tag_id = ?;";
    
    if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(check_stmt, 1, path_id);
        sqlite3_bind_int(check_stmt, 2, tag_id);
        
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            sqlite3_finalize(check_stmt);
            
            /* Get actual tag name (in case user was redirected to similar tag) */
            sqlite3_stmt *name_stmt;
            const char *name_sql = "SELECT name FROM tags WHERE id = ?;";
            char actual_name[MAX_TAG_LENGTH] = "";
            
            if (sqlite3_prepare_v2(db, name_sql, -1, &name_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(name_stmt, 1, tag_id);
                if (sqlite3_step(name_stmt) == SQLITE_ROW) {
                    strncpy(actual_name, (const char *)sqlite3_column_text(name_stmt, 0), 
                            sizeof(actual_name) - 1);
                }
                sqlite3_finalize(name_stmt);
            }
            
            printf("Path already has tag '%s'.\n", actual_name);
            return 0;
        }
        sqlite3_finalize(check_stmt);
    }
    
    /* Create association */
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO path_tags (path_id, tag_id) VALUES (?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    sqlite3_bind_int(stmt, 2, tag_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        /* Get actual tag name for display */
        sqlite3_stmt *name_stmt;
        const char *name_sql = "SELECT name FROM tags WHERE id = ?;";
        char actual_name[MAX_TAG_LENGTH] = "";
        
        if (sqlite3_prepare_v2(db, name_sql, -1, &name_stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(name_stmt, 1, tag_id);
            if (sqlite3_step(name_stmt) == SQLITE_ROW) {
                strncpy(actual_name, (const char *)sqlite3_column_text(name_stmt, 0), 
                        sizeof(actual_name) - 1);
            }
            sqlite3_finalize(name_stmt);
        }
        
        printf("Tagged: %s [%s]\n", path, actual_name);
        return 0;
    }
    return -1;
}

int untag_path(const char *path, const char *tag_name) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        fprintf(stderr, "Path not found in database: %s\n", path);
        return -1;
    }
    
    int tag_id = get_tag_id(tag_name);
    if (tag_id < 0) {
        fprintf(stderr, "Tag not found: %s\n", tag_name);
        return -1;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM path_tags WHERE path_id = ? AND tag_id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    sqlite3_bind_int(stmt, 2, tag_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        printf("Untagged: %s [%s]\n", path, tag_name);
        return 0;
    }
    return -1;
}

void list_all_tags() {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM tags ORDER BY name;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    printf("\n[All Tags]\n");
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  %s\n", sqlite3_column_text(stmt, 0));
        count++;
    }
    
    if (count == 0) {
        printf("  (no tags)\n");
    }
    printf("\nTotal: %d tags\n", count);
    
    sqlite3_finalize(stmt);
}

void list_path_tags(const char *path) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        fprintf(stderr, "Path not found in database: %s\n", path);
        return;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT t.name FROM tags t "
        "JOIN path_tags pt ON t.id = pt.tag_id "
        "WHERE pt.path_id = ? ORDER BY t.name;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    
    printf("\n[Tags for %s]\n", path);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  %s\n", sqlite3_column_text(stmt, 0));
        count++;
    }
    
    if (count == 0) {
        printf("  (no tags)\n");
    }
    printf("\n");
    
    sqlite3_finalize(stmt);
}

void search_tags_fuzzy(const char *query) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    int fuzzy_dist = get_int_setting("fuzzy_default_distance", DEFAULT_FUZZY_DISTANCE);
    
    /* Exact match */
    sqlite3_stmt *stmt;
    const char *sql_exact = "SELECT name FROM tags WHERE name = ? COLLATE NOCASE;";
    
    if (sqlite3_prepare_v2(db, sql_exact, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
        
        printf("\n[Exact Match - Tags]\n");
        int found = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("  %s\n", sqlite3_column_text(stmt, 0));
            found++;
        }
        if (!found) {
            printf("  (no exact match)\n");
        }
        sqlite3_finalize(stmt);
    }
    
    /* Substring match */
    const char *sql_substr = "SELECT name FROM tags WHERE name LIKE '%' || ? || '%' COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql_substr, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, max_results);
        
        printf("\n[Substring Match - Tags]\n");
        int found = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("  %s\n", sqlite3_column_text(stmt, 0));
            found++;
        }
        if (!found) {
            printf("  (no substring matches)\n");
        }
        sqlite3_finalize(stmt);
    }
    
    /* Fuzzy match */
    const char *sql_fuzzy = 
        "SELECT name, levenshtein(name, ?) as dist FROM tags "
        "WHERE dist <= ? ORDER BY dist, name LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql_fuzzy, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, fuzzy_dist);
        sqlite3_bind_int(stmt, 3, max_results);
        
        printf("\n[Fuzzy Match - Tags (distance <= %d)]\n", fuzzy_dist);
        int found = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *name = (const char *)sqlite3_column_text(stmt, 0);
            int dist = sqlite3_column_int(stmt, 1);
            printf("  %s (distance: %d)\n", name, dist);
            found++;
        }
        if (!found) {
            printf("  (no fuzzy matches)\n");
        }
        sqlite3_finalize(stmt);
    }
    
    printf("\n");
}

/* ============================================
 * Path Info
 * ============================================ */

void show_path_info(const char *path) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        fprintf(stderr, "Path not found in database: %s\n", path);
        return;
    }
    
    sqlite3_stmt *stmt;
    
    /* Get basic path info */
    const char *sql = "SELECT path, name, is_directory, size FROM paths WHERE id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    
    printf("\n[Path Info]\n");
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *full_path = (const char *)sqlite3_column_text(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        int is_dir = sqlite3_column_int(stmt, 2);
        
        printf("  Path:        %s\n", full_path);
        printf("  Name:        %s\n", name);
        printf("  Type:        %s\n", is_dir ? "Directory" : "File");
        
        if (!is_dir && sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            long long size = sqlite3_column_int64(stmt, 3);
            printf("  Size:        %lld bytes\n", size);
        }
    }
    sqlite3_finalize(stmt);
    
    /* Get categories */
    const char *cat_sql = 
        "SELECT c.name FROM categories c "
        "JOIN path_categories pc ON c.id = pc.category_id "
        "WHERE pc.path_id = ? ORDER BY c.name;";
    
    if (sqlite3_prepare_v2(db, cat_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, path_id);
        
        char categories[512] = "";
        int first = 1;
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *cat = (const char *)sqlite3_column_text(stmt, 0);
            if (!first) {
                strncat(categories, ", ", sizeof(categories) - strlen(categories) - 1);
            }
            strncat(categories, cat, sizeof(categories) - strlen(categories) - 1);
            first = 0;
        }
        
        printf("  Categories:  %s\n", strlen(categories) > 0 ? categories : "(none)");
        sqlite3_finalize(stmt);
    }
    
    /* Get tags */
    const char *tag_sql = 
        "SELECT t.name FROM tags t "
        "JOIN path_tags pt ON t.id = pt.tag_id "
        "WHERE pt.path_id = ? ORDER BY t.name;";
    
    if (sqlite3_prepare_v2(db, tag_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, path_id);
        
        char tags[512] = "";
        int first = 1;
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *tag = (const char *)sqlite3_column_text(stmt, 0);
            if (!first) {
                strncat(tags, ", ", sizeof(tags) - strlen(tags) - 1);
            }
            strncat(tags, tag, sizeof(tags) - strlen(tags) - 1);
            first = 0;
        }
        
        printf("  Tags:        %s\n", strlen(tags) > 0 ? tags : "(none)");
        sqlite3_finalize(stmt);
    }
    
    printf("\n");
}

/* ============================================
 * Search Functions - Paths
 * ============================================ */

void print_path_result(sqlite3_stmt *stmt, int show_distance) {
    const char *path = (const char *)sqlite3_column_text(stmt, 0);
    int is_dir = sqlite3_column_int(stmt, 1);
    
    if (is_dir) {
        printf("  [DIR]  %s", path);
    } else {
        long long size = sqlite3_column_int64(stmt, 2);
        printf("  [FILE] %s (%lld bytes)", path, size);
    }
    
    if (show_distance) {
        int dist = sqlite3_column_int(stmt, 3);
        printf(" (distance: %d)", dist);
    }
    
    printf("\n");
}

void search_paths_exact(const char *query) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE name = ? COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_results);
    
    printf("\n[Exact Match - Paths]\n");
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        print_path_result(stmt, 0);
        found++;
    }
    
    if (!found) {
        printf("  (no exact matches)\n");
    }
    
    sqlite3_finalize(stmt);
}

void search_paths_prefix(const char *query) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE name LIKE ? || '%' COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_results);
    
    printf("\n[Prefix Match - Paths]\n");
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        print_path_result(stmt, 0);
        found++;
    }
    
    if (!found) {
        printf("  (no prefix matches)\n");
    }
    
    sqlite3_finalize(stmt);
}

void search_paths_substring(const char *query) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE name LIKE '%' || ? || '%' COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_results);
    
    printf("\n[Substring Match - Paths]\n");
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        print_path_result(stmt, 0);
        found++;
    }
    
    if (!found) {
        printf("  (no substring matches)\n");
    }
    
    sqlite3_finalize(stmt);
}

void search_paths_fuzzy(const char *query, int max_distance) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    if (max_distance < 0) {
        max_distance = get_int_setting("fuzzy_default_distance", DEFAULT_FUZZY_DISTANCE);
    }
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size, levenshtein(name, ?) as dist "
        "FROM paths WHERE dist <= ? ORDER BY dist, name LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_distance);
    sqlite3_bind_int(stmt, 3, max_results);
    
    printf("\n[Fuzzy Match - Paths (distance <= %d)]\n", max_distance);
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        print_path_result(stmt, 1);
        found++;
    }
    
    if (!found) {
        printf("  (no fuzzy matches within distance %d)\n", max_distance);
    }
    
    sqlite3_finalize(stmt);
}

void search_paths_all(const char *query) {
    search_paths_exact(query);
    search_paths_prefix(query);
    search_paths_substring(query);
    search_paths_fuzzy(query, -1);
}

/* ============================================
 * Structured Search (find command)
 * ============================================ */

void structured_search(const char *category, const char *tag, const char *name) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    /* Build dynamic SQL based on provided filters */
    char sql[2048];
    int has_where = 0;
    
    strcpy(sql, "SELECT DISTINCT p.path, p.is_directory, p.size FROM paths p ");
    
    if (category && strlen(category) > 0) {
        strcat(sql, "JOIN path_categories pc ON p.id = pc.path_id ");
        strcat(sql, "JOIN categories c ON pc.category_id = c.id ");
    }
    
    if (tag && strlen(tag) > 0) {
        strcat(sql, "JOIN path_tags pt ON p.id = pt.path_id ");
        strcat(sql, "JOIN tags t ON pt.tag_id = t.id ");
    }
    
    if (category && strlen(category) > 0) {
        strcat(sql, has_where ? "AND " : "WHERE ");
        strcat(sql, "c.name = ? COLLATE NOCASE ");
        has_where = 1;
    }
    
    if (tag && strlen(tag) > 0) {
        strcat(sql, has_where ? "AND " : "WHERE ");
        strcat(sql, "t.name = ? COLLATE NOCASE ");
        has_where = 1;
    }
    
    if (name && strlen(name) > 0) {
        strcat(sql, has_where ? "AND " : "WHERE ");
        strcat(sql, "p.name LIKE '%' || ? || '%' COLLATE NOCASE ");
        has_where = 1;
    }
    
    strcat(sql, "ORDER BY p.path LIMIT ?;");
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    int param = 1;
    if (category && strlen(category) > 0) {
        sqlite3_bind_text(stmt, param++, category, -1, SQLITE_STATIC);
    }
    if (tag && strlen(tag) > 0) {
        sqlite3_bind_text(stmt, param++, tag, -1, SQLITE_STATIC);
    }
    if (name && strlen(name) > 0) {
        sqlite3_bind_text(stmt, param++, name, -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, param, max_results);
    
    printf("\n[Search Results]\n");
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        print_path_result(stmt, 0);
        found++;
    }
    
    if (!found) {
        printf("  (no matches)\n");
    }
    
    printf("\n");
    sqlite3_finalize(stmt);
}

/* ============================================
 * Statistics
 * ============================================ */

void show_stats() {
    sqlite3_stmt *stmt;
    
    printf("\n[Database Statistics]\n");
    
    /* Count paths */
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM paths;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Total paths:  %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    /* Count directories */
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM paths WHERE is_directory = 1;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Directories:  %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    /* Count files */
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM paths WHERE is_directory = 0;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Files:        %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    /* Count tags */
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM tags;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Tags:         %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    /* Count categories (total and in use) */
    int total_cats = 0, used_cats = 0;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM categories;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total_cats = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    sqlite3_prepare_v2(db, "SELECT COUNT(DISTINCT category_id) FROM path_categories;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        used_cats = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    printf("  Categories:   %d (%d in use)\n", total_cats, used_cats);
    
    printf("\n");
}

/* ============================================
 * CLI Parsing Helpers
 * ============================================ */

/*
 * Parse arguments for 'find' command.
 * Format: find --category X --tag Y --name Z
 */
void parse_find_args(const char *args, char *category, char *tag, char *name, size_t size) {
    category[0] = '\0';
    tag[0] = '\0';
    name[0] = '\0';
    
    char args_copy[MAX_INPUT_LENGTH];
    strncpy(args_copy, args, sizeof(args_copy) - 1);
    args_copy[sizeof(args_copy) - 1] = '\0';
    
    char *token = strtok(args_copy, " ");
    while (token) {
        if (strcmp(token, "--category") == 0 || strcmp(token, "-c") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                strncpy(category, token, size - 1);
                category[size - 1] = '\0';
            }
        } else if (strcmp(token, "--tag") == 0 || strcmp(token, "-t") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                strncpy(tag, token, size - 1);
                tag[size - 1] = '\0';
            }
        } else if (strcmp(token, "--name") == 0 || strcmp(token, "-n") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                strncpy(name, token, size - 1);
                name[size - 1] = '\0';
            }
        }
        token = strtok(NULL, " ");
    }
}

/*
 * Parse two space-separated arguments.
 */
void parse_two_args(const char *input, char *arg1, size_t size1, char *arg2, size_t size2) {
    arg1[0] = '\0';
    arg2[0] = '\0';
    
    /* Find last space - everything before is path, after is tag/category */
    const char *last_space = strrchr(input, ' ');
    
    if (last_space) {
        size_t path_len = last_space - input;
        if (path_len >= size1) path_len = size1 - 1;
        strncpy(arg1, input, path_len);
        arg1[path_len] = '\0';
        trim_whitespace(arg1);
        
        strncpy(arg2, last_space + 1, size2 - 1);
        arg2[size2 - 1] = '\0';
        trim_whitespace(arg2);
    }
}

/* ============================================
 * Interactive CLI
 * ============================================ */

void print_help() {
    printf("\n");
    printf("Path Commands:\n");
    printf("  add <directory>               - Add directory to database (recursive)\n");
    printf("  remove <path>                 - Remove path from database\n");
    printf("  info <path>                   - Show path details with tags and categories\n");
    printf("\n");
    printf("Search Commands:\n");
    printf("  search <term>                 - Search paths by name (all methods)\n");
    printf("  exact <term>                  - Exact match on path names\n");
    printf("  prefix <term>                 - Prefix match on path names\n");
    printf("  substring <term>              - Substring match on path names\n");
    printf("  fuzzy <term> [n]              - Fuzzy match with max distance n\n");
    printf("  find --category <cat> --tag <tag> --name <term>\n");
    printf("                                - Structured search with filters\n");
    printf("\n");
    printf("Tag Commands:\n");
    printf("  tag <path> <tagname>          - Add tag to path\n");
    printf("  untag <path> <tagname>        - Remove tag from path\n");
    printf("  tags [path]                   - List all tags, or tags on a path\n");
    printf("  tagsearch <term>              - Search existing tags\n");
    printf("\n");
    printf("Category Commands:\n");
    printf("  categorize <path> <category>  - Add category to path\n");
    printf("  uncategorize <path> <category>- Remove category from path\n");
    printf("  categories [path]             - List all categories, or categories on a path\n");
    printf("  create-category <name>        - Create new category\n");
    printf("\n");
    printf("Settings Commands:\n");
    printf("  set <key> <value>             - Modify a setting\n");
    printf("  get <key>                     - View a setting\n");
    printf("  settings                      - List all settings\n");
    printf("\n");
    printf("Utility Commands:\n");
    printf("  stats                         - Show database statistics\n");
    printf("  help                          - Show this help\n");
    printf("  quit / exit                   - Exit the program\n");
    printf("\n");
}

void run_interactive_cli() {
    char input[MAX_INPUT_LENGTH];
    char command[64];
    char argument[MAX_INPUT_LENGTH];
    
    printf("\nFileSearch v%d - Interactive CLI\n", 
           get_int_setting("app_version", DEFAULT_APP_VERSION));
    printf("Type 'help' for available commands.\n\n");
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            printf("\n");
            break;
        }
        
        trim_whitespace(input);
        
        if (strlen(input) == 0) {
            continue;
        }
        
        /* Parse command and argument */
        command[0] = '\0';
        argument[0] = '\0';
        
        char *space = strchr(input, ' ');
        if (space) {
            size_t cmd_len = space - input;
            if (cmd_len >= sizeof(command)) cmd_len = sizeof(command) - 1;
            strncpy(command, input, cmd_len);
            command[cmd_len] = '\0';
            
            strcpy(argument, space + 1);
            trim_whitespace(argument);
        } else {
            strncpy(command, input, sizeof(command) - 1);
            command[sizeof(command) - 1] = '\0';
        }
        
        /* Convert command to lowercase */
        str_to_lower(command);
        
        /* Execute command */
        if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            printf("Goodbye!\n");
            break;
        }
        else if (strcmp(command, "help") == 0) {
            print_help();
        }
        else if (strcmp(command, "add") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: add <directory>\n");
            } else {
                add_directory(argument);
            }
        }
        else if (strcmp(command, "remove") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: remove <path>\n");
            } else {
                remove_path_from_db(argument);
            }
        }
        else if (strcmp(command, "info") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: info <path>\n");
            } else {
                show_path_info(argument);
            }
        }
        else if (strcmp(command, "search") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: search <term>\n");
            } else {
                search_paths_all(argument);
            }
        }
        else if (strcmp(command, "exact") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: exact <term>\n");
            } else {
                search_paths_exact(argument);
            }
        }
        else if (strcmp(command, "prefix") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: prefix <term>\n");
            } else {
                search_paths_prefix(argument);
            }
        }
        else if (strcmp(command, "substring") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: substring <term>\n");
            } else {
                search_paths_substring(argument);
            }
        }
        else if (strcmp(command, "fuzzy") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: fuzzy <term> [max_distance]\n");
            } else {
                char term[256];
                int distance = -1;
                
                if (sscanf(argument, "%255s %d", term, &distance) < 1) {
                    printf("Usage: fuzzy <term> [max_distance]\n");
                } else {
                    search_paths_fuzzy(term, distance);
                }
            }
        }
        else if (strcmp(command, "find") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: find --category <cat> --tag <tag> --name <term>\n");
            } else {
                char category[256], tag[256], name[256];
                parse_find_args(argument, category, tag, name, sizeof(category));
                
                if (strlen(category) == 0 && strlen(tag) == 0 && strlen(name) == 0) {
                    printf("Usage: find --category <cat> --tag <tag> --name <term>\n");
                    printf("At least one filter is required.\n");
                } else {
                    structured_search(category, tag, name);
                }
            }
        }
        else if (strcmp(command, "tag") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: tag <path> <tagname>\n");
            } else {
                char path[MAX_PATH_LENGTH], tagname[MAX_TAG_LENGTH];
                parse_two_args(argument, path, sizeof(path), tagname, sizeof(tagname));
                
                if (strlen(path) == 0 || strlen(tagname) == 0) {
                    printf("Usage: tag <path> <tagname>\n");
                } else {
                    tag_path(path, tagname);
                }
            }
        }
        else if (strcmp(command, "untag") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: untag <path> <tagname>\n");
            } else {
                char path[MAX_PATH_LENGTH], tagname[MAX_TAG_LENGTH];
                parse_two_args(argument, path, sizeof(path), tagname, sizeof(tagname));
                
                if (strlen(path) == 0 || strlen(tagname) == 0) {
                    printf("Usage: untag <path> <tagname>\n");
                } else {
                    untag_path(path, tagname);
                }
            }
        }
        else if (strcmp(command, "tags") == 0) {
            if (strlen(argument) == 0) {
                list_all_tags();
            } else {
                list_path_tags(argument);
            }
        }
        else if (strcmp(command, "tagsearch") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: tagsearch <term>\n");
            } else {
                search_tags_fuzzy(argument);
            }
        }
        else if (strcmp(command, "categorize") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: categorize <path> <category>\n");
            } else {
                char path[MAX_PATH_LENGTH], catname[256];
                parse_two_args(argument, path, sizeof(path), catname, sizeof(catname));
                
                if (strlen(path) == 0 || strlen(catname) == 0) {
                    printf("Usage: categorize <path> <category>\n");
                } else {
                    categorize_path(path, catname);
                }
            }
        }
        else if (strcmp(command, "uncategorize") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: uncategorize <path> <category>\n");
            } else {
                char path[MAX_PATH_LENGTH], catname[256];
                parse_two_args(argument, path, sizeof(path), catname, sizeof(catname));
                
                if (strlen(path) == 0 || strlen(catname) == 0) {
                    printf("Usage: uncategorize <path> <category>\n");
                } else {
                    uncategorize_path(path, catname);
                }
            }
        }
        else if (strcmp(command, "categories") == 0) {
            if (strlen(argument) == 0) {
                list_all_categories();
            } else {
                list_path_categories(argument);
            }
        }
        else if (strcmp(command, "create-category") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: create-category <name>\n");
            } else {
                create_category(argument);
            }
        }
        else if (strcmp(command, "set") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: set <key> <value>\n");
            } else {
                char key[64], value[256];
                if (sscanf(argument, "%63s %255s", key, value) == 2) {
                    cmd_set_setting(key, value);
                } else {
                    printf("Usage: set <key> <value>\n");
                }
            }
        }
        else if (strcmp(command, "get") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: get <key>\n");
            } else {
                cmd_get_setting(argument);
            }
        }
        else if (strcmp(command, "settings") == 0) {
            show_all_settings();
        }
        else if (strcmp(command, "stats") == 0) {
            show_stats();
        }
        else {
            printf("Unknown command: '%s'. Type 'help' for available commands.\n", command);
        }
    }
}

/* ============================================
 * Command Line Argument Parsing
 * ============================================ */

void print_usage(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("\n");
    printf("Options:\n");
    printf("  --db <path>    Use specified database file\n");
    printf("  --help         Show this help message\n");
    printf("\n");
    printf("Default database location:\n");
    
    char default_path[MAX_PATH_LENGTH];
    if (get_default_db_path(default_path, sizeof(default_path)) == 0) {
        printf("  %s\n", default_path);
    } else {
        printf("  (could not determine default path)\n");
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    char db_path[MAX_PATH_LENGTH];
    int custom_db = 0;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--db") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --db requires a path argument\n");
                return 1;
            }
            strncpy(db_path, argv[i + 1], sizeof(db_path) - 1);
            db_path[sizeof(db_path) - 1] = '\0';
            custom_db = 1;
            i++;
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    /* Use default path if not specified */
    if (!custom_db) {
        if (get_default_db_path(db_path, sizeof(db_path)) != 0) {
            fprintf(stderr, "Error: Could not determine default database path.\n");
            return 1;
        }
    }
    
    /* Initialize database */
    if (init_database(db_path) != 0) {
        return 1;
    }
    
    /* Run interactive CLI */
    run_interactive_cli();
    
    /* Cleanup */
    sqlite3_close(db);
    return 0;
}
