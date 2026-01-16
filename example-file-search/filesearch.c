/*
 * FileSearch - Lightweight Path Management System
 * 
 * Features:
 * - Persistent SQLite database storage
 * - Add directories/files to database
 * - Search by name, tags (exact, prefix, substring, fuzzy)
 * - Cross-platform support (Windows/macOS/Linux)
 * 
 * Compile:
 *   Linux/macOS: gcc -o filesearch filesearch.c -lsqlite3
 *   Windows:     gcc -o filesearch.exe filesearch.c -lsqlite3
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
#define MAX_RESULTS 20
#define DB_FILENAME "filesearch.db"
#define APP_DIRNAME ".filesearch"

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

/* ============================================
 * Cross-Platform Path Handling
 * ============================================ */

/*
 * Get user's home directory.
 * Returns 0 on success, -1 on failure.
 */
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

/*
 * Build default database path: ~/.filesearch/filesearch.db
 * Returns 0 on success, -1 on failure.
 */
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

/*
 * Check if a directory exists.
 * Returns 1 if exists, 0 otherwise.
 */
int directory_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return 0;
}

/*
 * Check if a file exists.
 * Returns 1 if exists, 0 otherwise.
 */
int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

/*
 * Extract directory portion from a file path.
 */
void get_directory_from_path(const char *filepath, char *dir_buffer, size_t size) {
    strncpy(dir_buffer, filepath, size - 1);
    dir_buffer[size - 1] = '\0';
    
    char *last_sep = strrchr(dir_buffer, PATH_SEPARATOR);
    if (last_sep) {
        *last_sep = '\0';
    }
}

/*
 * Extract filename from a path.
 */
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

/* ============================================
 * Database Operations
 * ============================================ */

sqlite3 *db = NULL;

int init_database(const char *db_path) {
    // Check if parent directory exists
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
    
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database '%s': %s\n", db_path, sqlite3_errmsg(db));
        return -1;
    }
    
    // Register Levenshtein function
    rc = sqlite3_create_function(db, "levenshtein", 2, SQLITE_UTF8, NULL,
                                  sqlite_levenshtein, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot register function: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    // Create schema
    const char *schema = 
        "CREATE TABLE IF NOT EXISTS paths ("
        "  id INTEGER PRIMARY KEY,"
        "  path TEXT UNIQUE NOT NULL,"
        "  name TEXT NOT NULL,"
        "  is_directory INTEGER NOT NULL,"
        "  size INTEGER,"
        "  parent_path TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS tags ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT UNIQUE NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS path_tags ("
        "  path_id INTEGER NOT NULL,"
        "  tag_id INTEGER NOT NULL,"
        "  PRIMARY KEY (path_id, tag_id),"
        "  FOREIGN KEY (path_id) REFERENCES paths(id) ON DELETE CASCADE,"
        "  FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_path_name ON paths(name);"
        "CREATE INDEX IF NOT EXISTS idx_path_parent ON paths(parent_path);"
        "CREATE INDEX IF NOT EXISTS idx_path_is_dir ON paths(is_directory);"
        "CREATE INDEX IF NOT EXISTS idx_tag_name ON tags(name);";
    
    char *err_msg = NULL;
    rc = sqlite3_exec(db, schema, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Schema error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    printf("Database opened: %s\n", db_path);
    return 0;
}

/* ============================================
 * Directory Scanning and Adding
 * ============================================ */

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

int scan_directory_recursive(const char *dir_path, int *file_count, int *dir_count, int depth) {
    if (depth > 100) {  // Prevent infinite recursion
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
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Build full path
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
    // Normalize path - remove trailing separator if present
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
    
    // Begin transaction for faster inserts
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
    // Add the root directory itself
    const char *name = get_filename_from_path(normalized);
    add_path_to_db(normalized, name, 1, -1, NULL);
    
    int file_count = 0;
    int dir_count = 1;  // Count the root
    
    scan_directory_recursive(normalized, &file_count, &dir_count, 0);
    
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    
    printf("Added %d files and %d directories.\n\n", file_count, dir_count);
}

/* ============================================
 * Search Functions - Paths
 * ============================================ */

void search_paths_exact(const char *query) {
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE name = ? COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, MAX_RESULTS);
    
    printf("\n[Exact Match - Paths]\n");
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *path = (const char *)sqlite3_column_text(stmt, 0);
        int is_dir = sqlite3_column_int(stmt, 1);
        
        if (is_dir) {
            printf("  [DIR]  %s\n", path);
        } else {
            long long size = sqlite3_column_int64(stmt, 2);
            printf("  [FILE] %s (%lld bytes)\n", path, size);
        }
        found++;
    }
    
    if (!found) {
        printf("  (no exact matches)\n");
    }
    
    sqlite3_finalize(stmt);
}

void search_paths_prefix(const char *query) {
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE name LIKE ? || '%' COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, MAX_RESULTS);
    
    printf("\n[Prefix Match - Paths]\n");
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *path = (const char *)sqlite3_column_text(stmt, 0);
        int is_dir = sqlite3_column_int(stmt, 1);
        
        if (is_dir) {
            printf("  [DIR]  %s\n", path);
        } else {
            long long size = sqlite3_column_int64(stmt, 2);
            printf("  [FILE] %s (%lld bytes)\n", path, size);
        }
        found++;
    }
    
    if (!found) {
        printf("  (no prefix matches)\n");
    }
    
    sqlite3_finalize(stmt);
}

void search_paths_substring(const char *query) {
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE name LIKE '%' || ? || '%' COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, MAX_RESULTS);
    
    printf("\n[Substring Match - Paths]\n");
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *path = (const char *)sqlite3_column_text(stmt, 0);
        int is_dir = sqlite3_column_int(stmt, 1);
        
        if (is_dir) {
            printf("  [DIR]  %s\n", path);
        } else {
            long long size = sqlite3_column_int64(stmt, 2);
            printf("  [FILE] %s (%lld bytes)\n", path, size);
        }
        found++;
    }
    
    if (!found) {
        printf("  (no substring matches)\n");
    }
    
    sqlite3_finalize(stmt);
}

void search_paths_fuzzy(const char *query, int max_distance) {
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size, levenshtein(name, ?) as dist "
        "FROM paths "
        "WHERE dist <= ? "
        "ORDER BY dist, name "
        "LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_distance);
    sqlite3_bind_int(stmt, 3, MAX_RESULTS);
    
    printf("\n[Fuzzy Match - Paths (distance <= %d)]\n", max_distance);
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *path = (const char *)sqlite3_column_text(stmt, 0);
        int is_dir = sqlite3_column_int(stmt, 1);
        int dist = sqlite3_column_int(stmt, 3);
        
        if (is_dir) {
            printf("  [DIR]  %s (distance: %d)\n", path, dist);
        } else {
            long long size = sqlite3_column_int64(stmt, 2);
            printf("  [FILE] %s (%lld bytes, distance: %d)\n", path, size, dist);
        }
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
    search_paths_fuzzy(query, 2);
}

/* ============================================
 * Search Functions - Tags (Test Code)
 * ============================================ */

void search_tags_exact(const char *query) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM tags WHERE name = ? COLLATE NOCASE;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
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

void search_tags_fuzzy(const char *query, int max_distance) {
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT name, levenshtein(name, ?) as dist "
        "FROM tags "
        "WHERE dist <= ? "
        "ORDER BY dist, name "
        "LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_distance);
    sqlite3_bind_int(stmt, 3, MAX_RESULTS);
    
    printf("\n[Fuzzy Match - Tags (distance <= %d)]\n", max_distance);
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        int dist = sqlite3_column_int(stmt, 1);
        printf("  %s (distance: %d)\n", name, dist);
        found++;
    }
    
    if (!found) {
        printf("  (no fuzzy matches within distance %d)\n", max_distance);
    }
    
    sqlite3_finalize(stmt);
}

/* ============================================
 * Tag Management
 * ============================================ */

int add_tag(const char *tag_name) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR IGNORE INTO tags (name) VALUES (?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, tag_name, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

void load_tags_from_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Cannot open tag file: %s\n", filename);
        return;
    }
    
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
    char line[256];
    int count = 0;
    
    while (fgets(line, sizeof(line), file)) {
        trim_whitespace(line);
        if (strlen(line) > 0) {
            add_tag(line);
            count++;
        }
    }
    
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    fclose(file);
    
    printf("Loaded %d tags from '%s'\n", count, filename);
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
    printf("\nTotal: %d tags\n", count);
    
    sqlite3_finalize(stmt);
}

/* ============================================
 * Statistics
 * ============================================ */

void show_stats() {
    sqlite3_stmt *stmt;
    
    printf("\n[Database Statistics]\n");
    
    // Count paths
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM paths;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Total paths: %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    // Count directories
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM paths WHERE is_directory = 1;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Directories:  %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    // Count files
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM paths WHERE is_directory = 0;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Files:        %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    // Count tags
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM tags;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Tags:         %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    printf("\n");
}

/* ============================================
 * Interactive CLI
 * ============================================ */

void print_help() {
    printf("\n");
    printf("Commands:\n");
    printf("  add <directory>       - Add directory to database (recursive)\n");
    printf("  search <term>         - Search paths (exact, prefix, substring, fuzzy)\n");
    printf("  exact <term>          - Exact match on path names\n");
    printf("  prefix <term>         - Prefix match on path names\n");
    printf("  substring <term>      - Substring match on path names\n");
    printf("  fuzzy <term> [n]      - Fuzzy match with max distance n (default: 2)\n");
    printf("  stats                 - Show database statistics\n");
    printf("\n");
    printf("Tag Commands (test code):\n");
    printf("  loadtags <file>       - Load tags from text file\n");
    printf("  listtags              - List all tags\n");
    printf("  tagsearch <term>      - Fuzzy search tags\n");
    printf("\n");
    printf("  help                  - Show this help\n");
    printf("  quit / exit           - Exit the program\n");
    printf("\n");
}

void run_interactive_cli() {
    char input[MAX_INPUT_LENGTH];
    char command[64];
    char argument[MAX_INPUT_LENGTH];
    
    printf("\nFileSearch - Interactive CLI\n");
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
        
        // Parse command and argument
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
        
        // Convert command to lowercase
        for (char *p = command; *p; p++) {
            *p = tolower(*p);
        }
        
        // Execute command
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
                int distance = 2;
                
                if (sscanf(argument, "%255s %d", term, &distance) < 1) {
                    printf("Usage: fuzzy <term> [max_distance]\n");
                } else {
                    if (distance < 0) distance = 0;
                    if (distance > 10) distance = 10;
                    search_paths_fuzzy(term, distance);
                }
            }
        }
        else if (strcmp(command, "stats") == 0) {
            show_stats();
        }
        else if (strcmp(command, "loadtags") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: loadtags <file>\n");
            } else {
                load_tags_from_file(argument);
            }
        }
        else if (strcmp(command, "listtags") == 0) {
            list_all_tags();
        }
        else if (strcmp(command, "tagsearch") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: tagsearch <term>\n");
            } else {
                search_tags_exact(argument);
                search_tags_fuzzy(argument, 2);
            }
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
    
    // Parse command line arguments
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
    
    // Use default path if not specified
    if (!custom_db) {
        if (get_default_db_path(db_path, sizeof(db_path)) != 0) {
            fprintf(stderr, "Error: Could not determine default database path.\n");
            return 1;
        }
    }
    
    // Initialize database
    if (init_database(db_path) != 0) {
        return 1;
    }
    
    // Run interactive CLI
    run_interactive_cli();
    
    // Cleanup
    sqlite3_close(db);
    return 0;
}
