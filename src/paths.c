/*
 * FileSearch - Path Operations Implementation
 */

#include "paths.h"
#include "database.h"

#ifdef _WIN32
#include <wchar.h>
#endif

/* ============================================
 * Path ID Lookup
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

/* ============================================
 * Path CRUD Operations
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

/* ============================================
 * Directory Scanning
 * ============================================ */

/*
 * Recursively scan a directory with depth control.
 * 
 * max_depth values:
 *   -1 = unlimited recursion (up to MAX_RECURSION_DEPTH safety limit)
 *    0 = add directory only, no contents
 *    1 = add immediate children only
 *    2 = add children and grandchildren
 *    etc.
 */

#ifdef _WIN32
/* Windows implementation using wide character APIs for Unicode support */
int scan_directory(const char *dir_path, int *file_count, int *dir_count, 
                   int current_depth, int max_depth) {
    /* Safety limit to prevent infinite recursion */
    if (current_depth > MAX_RECURSION_DEPTH) {
        fprintf(stderr, "Warning: Maximum recursion depth reached at %s\n", dir_path);
        return 0;
    }
    
    /* Check user-specified depth limit */
    if (max_depth >= 0 && current_depth > max_depth) {
        return 0;
    }
    
    /* Convert path to wide string */
    wchar_t *wide_path = utf8_to_wide(dir_path);
    if (!wide_path) {
        fprintf(stderr, "Cannot convert path to wide string: %s\n", dir_path);
        return -1;
    }
    
    _WDIR *dir = _wopendir(wide_path);
    free(wide_path);
    
    if (!dir) {
        fprintf(stderr, "Cannot open directory: %s\n", dir_path);
        return -1;
    }
    
    struct _wdirent *entry;
    char full_path[MAX_PATH_LENGTH];
    struct _stat st;
    
    while ((entry = _wreaddir(dir)) != NULL) {
        /* Skip . and .. */
        if (wcscmp(entry->d_name, L".") == 0 || wcscmp(entry->d_name, L"..") == 0) {
            continue;
        }
        
        /* Convert entry name to UTF-8 */
        char *entry_name = wide_to_utf8(entry->d_name);
        if (!entry_name) {
            continue;
        }
        
        /* Build full path */
        snprintf(full_path, sizeof(full_path), "%s%s%s", 
                 dir_path, PATH_SEPARATOR_STR, entry_name);
        
        /* Convert full path to wide for stat */
        wchar_t *wide_full = utf8_to_wide(full_path);
        if (!wide_full) {
            free(entry_name);
            continue;
        }
        
        if (_wstat(wide_full, &st) != 0) {
            fprintf(stderr, "Cannot stat: %s\n", full_path);
            free(wide_full);
            free(entry_name);
            continue;
        }
        free(wide_full);
        
        int is_dir = (st.st_mode & _S_IFDIR) != 0;
        long long size = is_dir ? -1 : (long long)st.st_size;
        
        add_path_to_db(full_path, entry_name, is_dir, size, dir_path);
        
        if (is_dir) {
            (*dir_count)++;
            /* Recurse into subdirectory */
            scan_directory(full_path, file_count, dir_count, 
                           current_depth + 1, max_depth);
        } else {
            (*file_count)++;
        }
        
        free(entry_name);
    }
    
    _wclosedir(dir);
    return 0;
}

#else
/* Unix/Linux/macOS implementation */
int scan_directory(const char *dir_path, int *file_count, int *dir_count, 
                   int current_depth, int max_depth) {
    /* Safety limit to prevent infinite recursion */
    if (current_depth > MAX_RECURSION_DEPTH) {
        fprintf(stderr, "Warning: Maximum recursion depth reached at %s\n", dir_path);
        return 0;
    }
    
    /* Check user-specified depth limit */
    if (max_depth >= 0 && current_depth > max_depth) {
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
        /* Skip . and .. */
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
            /* Recurse into subdirectory */
            scan_directory(full_path, file_count, dir_count, 
                           current_depth + 1, max_depth);
        } else {
            (*file_count)++;
        }
    }
    
    closedir(dir);
    return 0;
}
#endif

void add_directory(const char *path, int max_depth) {
    char normalized[MAX_PATH_LENGTH];
    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    
    /* Remove trailing slashes */
    size_t len = strlen(normalized);
    while (len > 1 && (normalized[len-1] == '/' || normalized[len-1] == '\\')) {
        normalized[--len] = '\0';
    }
    
    if (!directory_exists(normalized)) {
        fprintf(stderr, "Error: '%s' is not a valid directory.\n", normalized);
        return;
    }
    
    /* Display appropriate message based on depth */
    if (max_depth == 0) {
        printf("Adding directory (no recursion): %s\n", normalized);
    } else if (max_depth > 0) {
        printf("Scanning directory (depth %d): %s\n", max_depth, normalized);
    } else {
        printf("Scanning directory (unlimited depth): %s\n", normalized);
    }
    
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
    /* Add the root directory itself */
    const char *name = get_filename_from_path(normalized);
    add_path_to_db(normalized, name, 1, -1, NULL);
    
    int file_count = 0;
    int dir_count = 1;  /* Count the root directory */
    
    /* Scan contents starting at depth 0 */
    scan_directory(normalized, &file_count, &dir_count, 0, max_depth);
    
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    
    printf("Added %d files and %d directories.\n\n", file_count, dir_count);
}

/* ============================================
 * Path Information Display
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
