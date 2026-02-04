/*
 * FileSearch - Path Operations Implementation
 */

#include "paths.h"
#include "database.h"
#include "categories.h"
#include "tags.h"

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
    
    if (rc != SQLITE_DONE) {
        return -1;
    }
    
    /* Check if a row was actually inserted (not ignored due to duplicate) */
    if (sqlite3_changes(db) > 0) {
        /* Get the path_id and assign Uncategorized category */
        int path_id = get_path_id(path);
        if (path_id >= 0) {
            assign_uncategorized(path_id);
        }
    }
    
    return 0;
}

int remove_path_from_db(const char *path) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        printf_utf8("Path not found in database: %s\n", path);
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
        printf_utf8("Removed: %s\n", path);
        return 0;
    }
    return -1;
}

int remove_contents_under_path(const char *dir_path) {
    char normalized[MAX_PATH_LENGTH];
    strncpy(normalized, dir_path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    
    /* Remove trailing slashes */
    size_t len = strlen(normalized);
    while (len > 1 && (normalized[len-1] == '/' || normalized[len-1] == '\\')) {
        normalized[--len] = '\0';
    }
    
    /* First, count how many items will be removed */
    sqlite3_stmt *count_stmt;
    const char *count_sql = 
        "SELECT COUNT(*) FROM paths WHERE path LIKE ? || '%' AND path != ?;";
    
    if (sqlite3_prepare_v2(db, count_sql, -1, &count_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    /* Build pattern: dir_path + separator */
    char pattern[MAX_PATH_LENGTH];
    snprintf(pattern, sizeof(pattern), "%s%s", normalized, PATH_SEPARATOR_STR);
    
    sqlite3_bind_text(count_stmt, 1, pattern, -1, SQLITE_STATIC);
    sqlite3_bind_text(count_stmt, 2, normalized, -1, SQLITE_STATIC);
    
    int count = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(count_stmt, 0);
    }
    sqlite3_finalize(count_stmt);
    
    if (count == 0) {
        printf_utf8("No contents found under: %s\n", normalized);
        return 0;
    }
    
    /* Ask for confirmation */
    printf_utf8("This will remove %d item(s) under: %s\n", count, normalized);
    if (!get_confirmation("Proceed with removal?")) {
        printf("Cancelled.\n");
        return -1;
    }
    
    /* Delete all paths under the directory */
    sqlite3_stmt *delete_stmt;
    const char *delete_sql = 
        "DELETE FROM paths WHERE path LIKE ? || '%' AND path != ?;";
    
    if (sqlite3_prepare_v2(db, delete_sql, -1, &delete_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    sqlite3_bind_text(delete_stmt, 1, pattern, -1, SQLITE_STATIC);
    sqlite3_bind_text(delete_stmt, 2, normalized, -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(delete_stmt);
    sqlite3_finalize(delete_stmt);
    
    if (rc == SQLITE_DONE) {
        int deleted = sqlite3_changes(db);
        printf_utf8("Removed %d item(s) under: %s\n", deleted, normalized);
        return deleted;
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
    /* max_depth 0 means don't scan contents, 1 means scan one level, etc. */
    if (max_depth >= 0 && current_depth >= max_depth) {
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
    /* max_depth 0 means don't scan contents, 1 means scan one level, etc. */
    if (max_depth >= 0 && current_depth >= max_depth) {
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
        printf_utf8("Error: '%s' is not a valid directory.\n", normalized);
        return;
    }
    
    /* Display appropriate message based on depth */
    if (max_depth == 0) {
        printf_utf8("Adding directory (no recursion): %s\n", normalized);
    } else if (max_depth > 0) {
        printf_utf8("Scanning directory (depth %d): %s\n", max_depth, normalized);
    } else {
        printf_utf8("Scanning directory (unlimited depth): %s\n", normalized);
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
    
    printf_utf8("Added %d files and %d directories.\n\n", file_count, dir_count);
}

/* ============================================
 * Single File Addition
 * ============================================ */

int add_file(const char *path) {
    char normalized[MAX_PATH_LENGTH];
    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    
    /* Remove trailing slashes (shouldn't be there for files, but just in case) */
    size_t len = strlen(normalized);
    while (len > 1 && (normalized[len-1] == '/' || normalized[len-1] == '\\')) {
        normalized[--len] = '\0';
    }
    
    if (!is_regular_file(normalized)) {
        printf_utf8("Error: '%s' is not a valid file.\n", normalized);
        return -1;
    }
    
    /* Get file info */
    const char *name = get_filename_from_path(normalized);
    long long size = get_file_size(normalized);
    
    /* Get parent directory path */
    char parent_path[MAX_PATH_LENGTH];
    get_directory_from_path(normalized, parent_path, sizeof(parent_path));
    
    /* Add to database */
    int result = add_path_to_db(normalized, name, 0, size, parent_path);
    
    if (result == 0) {
        printf_utf8("Added file: %s\n", normalized);
    }
    
    return result;
}

/* ============================================
 * Unified Add (auto-detects file vs directory)
 * ============================================ */

void add_path(const char *path, int max_depth) {
    char normalized[MAX_PATH_LENGTH];
    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    
    /* Remove trailing slashes */
    size_t len = strlen(normalized);
    while (len > 1 && (normalized[len-1] == '/' || normalized[len-1] == '\\')) {
        normalized[--len] = '\0';
    }
    
    if (directory_exists(normalized)) {
        add_directory(normalized, max_depth);
    } else if (is_regular_file(normalized)) {
        /* Ignore -d flag for files */
        add_file(normalized);
    } else {
        printf_utf8("Error: '%s' does not exist or is not accessible.\n", normalized);
    }
}

/* ============================================
 * Unified Add with Metadata
 * ============================================ */

/* ============================================
 * Auto-tag from Filename Patterns
 * ============================================ */

/*
 * Extract tags from filename pattern [tag1][tag2]... and apply them.
 * Returns: number of tags applied
 */
int auto_tag_path(int path_id, const char *name) {
    ExtractedTags extracted;
    int count = extract_tags_from_name(name, &extracted);
    
    if (count == 0) {
        return 0;
    }
    
    int applied = 0;
    for (int i = 0; i < extracted.count; i++) {
        int tag_id = get_or_create_tag_with_check(extracted.tags[i]);
        if (tag_id >= 0) {
            tag_path_by_id(path_id, tag_id);
            applied++;
        }
    }
    
    return applied;
}

/* ============================================
 * Unified Add with Metadata
 * ============================================ */

void add_path_with_metadata(const char *path, int max_depth,
                            const char **categories, int category_count,
                            const char **tags, int tag_count,
                            int auto_tag) {
    char normalized[MAX_PATH_LENGTH];
    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    
    /* Remove trailing slashes */
    size_t len = strlen(normalized);
    while (len > 1 && (normalized[len-1] == '/' || normalized[len-1] == '\\')) {
        normalized[--len] = '\0';
    }
    
    /* First, add the path normally */
    add_path(normalized, max_depth);
    
    /* Pre-resolve category IDs and check validity */
    int cat_ids[16];
    int valid_cat_count = 0;
    int has_non_uncategorized = 0;
    
    for (int i = 0; i < category_count && i < 16; i++) {
        if (categories[i] && categories[i][0] != '\0') {
            int cat_id = get_category_id(categories[i]);
            if (cat_id >= 0) {
                cat_ids[valid_cat_count++] = cat_id;
                
                /* Check if this is not "Uncategorized" */
                char lower_name[MAX_TAG_LENGTH];
                strncpy(lower_name, categories[i], sizeof(lower_name) - 1);
                lower_name[sizeof(lower_name) - 1] = '\0';
                str_to_lower(lower_name);
                if (strcmp(lower_name, "uncategorized") != 0) {
                    has_non_uncategorized = 1;
                }
                
                printf_utf8("  Category: %s\n", categories[i]);
            } else {
                printf_utf8("  Category not found: %s (use 'create-category' first)\n", categories[i]);
            }
        }
    }
    
    /* Pre-resolve tag IDs */
    int tag_ids[32];
    int valid_tag_count = 0;
    
    for (int i = 0; i < tag_count && i < 32; i++) {
        if (tags[i] && tags[i][0] != '\0') {
            int tag_id = get_or_create_tag_with_check(tags[i]);
            if (tag_id >= 0) {
                tag_ids[valid_tag_count++] = tag_id;
                printf_utf8("  Tag: %s\n", tags[i]);
            }
        }
    }
    
    /* If no valid categories, tags, or auto-tag, we're done */
    if (valid_cat_count == 0 && valid_tag_count == 0 && !auto_tag) {
        return;
    }
    
    /* Query all paths under the root (including the root itself) */
    sqlite3_stmt *stmt;
    char pattern[MAX_PATH_LENGTH];
    snprintf(pattern, sizeof(pattern), "%s%s", normalized, PATH_SEPARATOR_STR);
    
    const char *sql = 
        "SELECT id, name FROM paths WHERE path = ? OR path LIKE ? || '%';";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, normalized, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
    
    int items_updated = 0;
    int auto_tags_applied = 0;
    
    /* Apply categories, tags, and auto-tag to each path */
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int path_id = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        
        /* Apply categories */
        for (int i = 0; i < valid_cat_count; i++) {
            categorize_path_by_id(path_id, cat_ids[i]);
        }
        
        /* Remove Uncategorized if we assigned a real category */
        if (has_non_uncategorized) {
            remove_uncategorized(path_id);
        }
        
        /* Apply tags */
        for (int i = 0; i < valid_tag_count; i++) {
            tag_path_by_id(path_id, tag_ids[i]);
        }
        
        /* Auto-tag from filename pattern */
        if (auto_tag && name) {
            auto_tags_applied += auto_tag_path(path_id, name);
        }
        
        items_updated++;
    }
    
    sqlite3_finalize(stmt);
    
    if (items_updated > 1) {
        printf_utf8("Applied to %d items.\n", items_updated);
    }
    
    if (auto_tag && auto_tags_applied > 0) {
        printf_utf8("Auto-tagged: %d tag(s) extracted from filenames.\n", auto_tags_applied);
    }
}

/* ============================================
 * Path Information Display
 * ============================================ */

void show_path_info(const char *path) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        printf_utf8("Path not found in database: %s\n", path);
        return;
    }
    
    sqlite3_stmt *stmt;
    
    /* Get basic path info */
    const char *sql = "SELECT path, name, is_directory, size FROM paths WHERE id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    
    printf_utf8("\n[Path Info]\n");
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *full_path = (const char *)sqlite3_column_text(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        int is_dir = sqlite3_column_int(stmt, 2);
        
        printf_utf8("  Path:        %s\n", full_path);
        printf_utf8("  Name:        %s\n", name);
        printf_utf8("  Type:        %s\n", is_dir ? "Directory" : "File");
        
        if (!is_dir && sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            long long size = sqlite3_column_int64(stmt, 3);
            printf_utf8("  Size:        %lld bytes\n", size);
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
        
        printf_utf8("  Categories:  %s\n", strlen(categories) > 0 ? categories : "(none)");
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
        
        printf_utf8("  Tags:        %s\n", strlen(tags) > 0 ? tags : "(none)");
        sqlite3_finalize(stmt);
    }
    
    printf_utf8("\n");
}

/* ============================================
 * Check/Sync Operations
 * ============================================ */

/* Temporary storage for filesystem scan */
#define MAX_FS_PATHS 10000
static struct {
    char paths[MAX_FS_PATHS][MAX_PATH_LENGTH];
    int is_dir[MAX_FS_PATHS];
    long long sizes[MAX_FS_PATHS];
    int count;
} g_fs_scan = { .count = 0 };

static void clear_fs_scan(void) {
    g_fs_scan.count = 0;
}

static void store_fs_path(const char *path, int is_directory, long long size) {
    if (g_fs_scan.count >= MAX_FS_PATHS) return;
    
    strncpy(g_fs_scan.paths[g_fs_scan.count], path, MAX_PATH_LENGTH - 1);
    g_fs_scan.paths[g_fs_scan.count][MAX_PATH_LENGTH - 1] = '\0';
    g_fs_scan.is_dir[g_fs_scan.count] = is_directory;
    g_fs_scan.sizes[g_fs_scan.count] = size;
    g_fs_scan.count++;
}

/*
 * Recursively scan filesystem and collect paths (without adding to DB).
 */
static int scan_filesystem(const char *dir_path, int current_depth, int max_depth) {
    if (max_depth >= 0 && current_depth > max_depth) {
        return 0;
    }
    
    if (current_depth > MAX_RECURSION_DEPTH) {
        return 0;
    }
    
#ifdef _WIN32
    char search_path[MAX_PATH_LENGTH];
    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);
    
    wchar_t *wide_path = utf8_to_wide(search_path);
    if (!wide_path) return 0;
    
    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(wide_path, &find_data);
    free(wide_path);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }
    
    do {
        if (wcscmp(find_data.cFileName, L".") == 0 || 
            wcscmp(find_data.cFileName, L"..") == 0) {
            continue;
        }
        
        char *name_utf8 = wide_to_utf8(find_data.cFileName);
        if (!name_utf8) continue;
        
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "%s%s%s", 
                 dir_path, PATH_SEPARATOR_STR, name_utf8);
        
        int is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        long long size = -1;
        if (!is_dir) {
            LARGE_INTEGER li;
            li.LowPart = find_data.nFileSizeLow;
            li.HighPart = find_data.nFileSizeHigh;
            size = li.QuadPart;
        }
        
        store_fs_path(full_path, is_dir, size);
        
        if (is_dir) {
            scan_filesystem(full_path, current_depth + 1, max_depth);
        }
        
        free(name_utf8);
    } while (FindNextFileW(hFind, &find_data));
    
    FindClose(hFind);
#else
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, sizeof(full_path), "%s%s%s", 
                 dir_path, PATH_SEPARATOR_STR, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }
        
        int is_dir = S_ISDIR(st.st_mode);
        long long size = is_dir ? -1 : st.st_size;
        
        store_fs_path(full_path, is_dir, size);
        
        if (is_dir) {
            scan_filesystem(full_path, current_depth + 1, max_depth);
        }
    }
    
    closedir(dir);
#endif
    
    return 0;
}

/*
 * Check if a path exists in the filesystem scan results.
 */
static int path_in_fs_scan(const char *path) {
    for (int i = 0; i < g_fs_scan.count; i++) {
        if (strcmp(g_fs_scan.paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * Check if a path exists in the database.
 */
static int path_in_db(const char *path) {
    return get_path_id(path) >= 0;
}

/*
 * Check a path against the database.
 * Populates g_check_new and g_check_missing buffers.
 */
void check_path(const char *path, int max_depth) {
    char normalized[MAX_PATH_LENGTH];
    strncpy(normalized, path, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    
    /* Remove trailing slashes */
    size_t len = strlen(normalized);
    while (len > 1 && (normalized[len-1] == '/' || normalized[len-1] == '\\')) {
        normalized[--len] = '\0';
    }
    
    if (!directory_exists(normalized)) {
        printf_utf8("Directory not found: %s\n", normalized);
        return;
    }
    
    clear_check_results();
    clear_fs_scan();
    
    /* Store the root itself */
    store_fs_path(normalized, 1, -1);
    
    /* Scan filesystem */
    if (max_depth < 0) {
        printf_utf8("\nChecking: %s (unlimited depth)\n", normalized);
    } else {
        printf_utf8("\nChecking: %s (depth %d)\n", normalized, max_depth);
    }
    
    scan_filesystem(normalized, 0, max_depth);
    
    /* Find NEW items: in filesystem but not in DB */
    for (int i = 0; i < g_fs_scan.count; i++) {
        if (!path_in_db(g_fs_scan.paths[i])) {
            store_check_new(g_fs_scan.paths[i], g_fs_scan.is_dir[i], g_fs_scan.sizes[i]);
        }
    }
    
    /* Find MISSING items: in DB but not in filesystem */
    sqlite3_stmt *stmt;
    char pattern[MAX_PATH_LENGTH];
    snprintf(pattern, sizeof(pattern), "%s%s", normalized, PATH_SEPARATOR_STR);
    
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE path = ? OR path LIKE ? || '%';";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, normalized, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_STATIC);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *db_path = (const char *)sqlite3_column_text(stmt, 0);
            int is_dir = sqlite3_column_int(stmt, 1);
            long long size = sqlite3_column_int64(stmt, 2);
            
            if (!path_in_fs_scan(db_path)) {
                /* Check if parent is already marked missing (auto-include children) */
                int parent_missing = 0;
                for (int i = 0; i < g_check_missing.count; i++) {
                    if (g_check_missing.items[i].is_directory) {
                        size_t plen = strlen(g_check_missing.items[i].path);
                        if (strncmp(db_path, g_check_missing.items[i].path, plen) == 0 &&
                            (db_path[plen] == '/' || db_path[plen] == '\\')) {
                            parent_missing = 1;
                            break;
                        }
                    }
                }
                
                if (!parent_missing) {
                    store_check_missing(db_path, is_dir, size);
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    
    /* Display results */
    show_check_new();
    show_check_missing();
    printf("\n");
}

/*
 * Check a category's root directories.
 */
void check_category(const char *category_name, const char *specific_root) {
    char roots[16][MAX_PATH_LENGTH];
    int root_count = get_category_roots(category_name, roots, 16);
    
    if (root_count == 0) {
        printf_utf8("No roots defined for category '%s'\n", category_name);
        printf("Use 'set-root %s <path>' to add a root directory.\n", category_name);
        return;
    }
    
    if (specific_root && specific_root[0] != '\0') {
        /* Check only the specified root */
        int found = 0;
        for (int i = 0; i < root_count; i++) {
            if (strcmp(roots[i], specific_root) == 0) {
                found = 1;
                break;
            }
        }
        
        if (!found) {
            printf_utf8("'%s' is not a root for category '%s'\n", specific_root, category_name);
            return;
        }
        
        printf_utf8("Checking category '%s' (1 root)\n", category_name);
        check_path(specific_root, -1);
    } else {
        /* Check all roots */
        printf_utf8("Checking category '%s' (%d root%s)\n", 
                   category_name, root_count, root_count > 1 ? "s" : "");
        
        /* Accumulate results across all roots */
        clear_check_results();
        
        for (int i = 0; i < root_count; i++) {
            printf_utf8("\n  Root %d: %s\n", i + 1, roots[i]);
            
            /* Temporarily save current counts */
            int prev_new = g_check_new.count;
            int prev_missing = g_check_missing.count;
            
            check_path(roots[i], -1);
            
            printf_utf8("    New: %d, Missing: %d\n", 
                       g_check_new.count - prev_new,
                       g_check_missing.count - prev_missing);
        }
        
        /* Show combined totals */
        printf("\n[Combined Totals]\n");
        show_check_new();
        show_check_missing();
        printf("\n");
    }
}

/*
 * Add items from g_check_new buffer to database with metadata.
 */
void add_check_new_items(const char **categories, int category_count,
                         const char **tags, int tag_count, int auto_tag) {
    if (g_check_new.count == 0) {
        printf("No new items to add.\n");
        return;
    }
    
    /* Pre-resolve category IDs */
    int cat_ids[16];
    int valid_cat_count = 0;
    int has_non_uncategorized = 0;
    
    for (int i = 0; i < category_count && i < 16; i++) {
        if (categories[i] && categories[i][0] != '\0') {
            int cat_id = get_category_id(categories[i]);
            if (cat_id >= 0) {
                cat_ids[valid_cat_count++] = cat_id;
                
                char lower_name[MAX_TAG_LENGTH];
                strncpy(lower_name, categories[i], sizeof(lower_name) - 1);
                lower_name[sizeof(lower_name) - 1] = '\0';
                str_to_lower(lower_name);
                if (strcmp(lower_name, "uncategorized") != 0) {
                    has_non_uncategorized = 1;
                }
                printf_utf8("  Category: %s\n", categories[i]);
            } else {
                printf_utf8("  Category not found: %s\n", categories[i]);
            }
        }
    }
    
    /* Pre-resolve tag IDs */
    int tag_ids[32];
    int valid_tag_count = 0;
    
    for (int i = 0; i < tag_count && i < 32; i++) {
        if (tags[i] && tags[i][0] != '\0') {
            int tag_id = get_or_create_tag_with_check(tags[i]);
            if (tag_id >= 0) {
                tag_ids[valid_tag_count++] = tag_id;
                printf_utf8("  Tag: %s\n", tags[i]);
            }
        }
    }
    
    int added = 0;
    int auto_tags_applied = 0;
    
    for (int i = 0; i < g_check_new.count; i++) {
        CheckItem *item = &g_check_new.items[i];
        const char *name = get_filename_from_path(item->path);
        
        /* Determine parent path */
        char parent[MAX_PATH_LENGTH] = "";
        const char *last_sep = strrchr(item->path, PATH_SEPARATOR);
        if (last_sep && last_sep != item->path) {
            size_t parent_len = last_sep - item->path;
            strncpy(parent, item->path, parent_len);
            parent[parent_len] = '\0';
        }
        
        /* Add to database */
        if (add_path_to_db(item->path, name, item->is_directory, item->size, 
                           parent[0] ? parent : NULL) == 0) {
            int path_id = get_path_id(item->path);
            if (path_id >= 0) {
                /* Apply categories */
                for (int c = 0; c < valid_cat_count; c++) {
                    categorize_path_by_id(path_id, cat_ids[c]);
                }
                if (has_non_uncategorized) {
                    remove_uncategorized(path_id);
                }
                
                /* Apply tags */
                for (int t = 0; t < valid_tag_count; t++) {
                    tag_path_by_id(path_id, tag_ids[t]);
                }
                
                /* Auto-tag */
                if (auto_tag) {
                    auto_tags_applied += auto_tag_path(path_id, name);
                }
            }
            added++;
        }
    }
    
    printf("\nAdded %d items.\n", added);
    if (auto_tag && auto_tags_applied > 0) {
        printf("Auto-tagged: %d tag(s) extracted from filenames.\n", auto_tags_applied);
    }
    
    g_check_new.count = 0;
}

/*
 * Remove items from g_check_missing buffer from database.
 * Also removes children of missing directories.
 */
void remove_check_missing_items(void) {
    if (g_check_missing.count == 0) {
        printf("No missing items to remove.\n");
        return;
    }
    
    int removed = 0;
    int children_removed = 0;
    
    for (int i = 0; i < g_check_missing.count; i++) {
        CheckItem *item = &g_check_missing.items[i];
        
        if (item->is_directory) {
            /* Count and remove children first */
            sqlite3_stmt *count_stmt;
            const char *count_sql = 
                "SELECT COUNT(*) FROM paths WHERE path LIKE ? || '%' AND path != ?;";
            
            char pattern[MAX_PATH_LENGTH];
            snprintf(pattern, sizeof(pattern), "%s%s", item->path, PATH_SEPARATOR_STR);
            
            if (sqlite3_prepare_v2(db, count_sql, -1, &count_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(count_stmt, 1, pattern, -1, SQLITE_STATIC);
                sqlite3_bind_text(count_stmt, 2, item->path, -1, SQLITE_STATIC);
                
                if (sqlite3_step(count_stmt) == SQLITE_ROW) {
                    children_removed += sqlite3_column_int(count_stmt, 0);
                }
                sqlite3_finalize(count_stmt);
            }
            
            /* Remove children */
            remove_contents_under_path(item->path);
        }
        
        /* Remove the item itself */
        int path_id = get_path_id(item->path);
        if (path_id >= 0) {
            sqlite3_stmt *stmt;
            const char *sql = "DELETE FROM paths WHERE id = ?;";
            
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, path_id);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    removed++;
                }
                sqlite3_finalize(stmt);
            }
        }
    }
    
    if (children_removed > 0) {
        printf("Removed %d items (+ %d children).\n", removed, children_removed);
    } else {
        printf("Removed %d items.\n", removed);
    }
    
    g_check_missing.count = 0;
}
