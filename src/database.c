/*
 * FileSearch - Database Operations Implementation
 */

#include "database.h"

/* Global database handle */
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
        const char *val = (const char *)sqlite3_column_text(stmt, 0);
        if (val) result = atoi(val);
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

void show_all_settings(void) {
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
 * Schema Management
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

int create_schema(void) {
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

int insert_default_settings(void) {
    set_int_setting("schema_version", DEFAULT_SCHEMA_VERSION);
    set_int_setting("app_version", DEFAULT_APP_VERSION);
    set_int_setting("similarity_threshold", DEFAULT_SIMILARITY_THRESHOLD);
    set_int_setting("max_results", DEFAULT_MAX_RESULTS);
    set_int_setting("fuzzy_default_distance", DEFAULT_FUZZY_DISTANCE);
    set_int_setting("default_scan_depth", DEFAULT_SCAN_DEPTH);
    return 0;
}

int insert_default_categories(void) {
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

/* ============================================
 * Database Initialization
 * ============================================ */

int init_database(const char *db_path) {
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
        
        if (create_schema() != 0) {
            return -1;
        }
        
        insert_default_settings();
        insert_default_categories();
        
        printf("Database initialized with default settings and categories.\n");
    } else {
        printf("Database opened: %s\n", db_path);
        
        int current_version = get_int_setting("schema_version", 0);
        
        if (current_version == 0 && !table_exists("settings")) {
            printf("\nDatabase schema update required.\n");
            printf("This will add category support and settings to your existing data.\n");
            printf("Existing paths will be assigned to 'Uncategorized'.\n\n");
            
            if (!get_confirmation("Proceed with migration?")) {
                printf("Migration cancelled. Exiting.\n");
                sqlite3_close(db);
                db = NULL;
                return -1;
            }
            
            if (create_schema() != 0) {
                return -1;
            }
            
            insert_default_settings();
            insert_default_categories();
            
            const char *migrate_sql = 
                "INSERT OR IGNORE INTO path_categories (path_id, category_id) "
                "SELECT p.id, c.id FROM paths p, categories c WHERE c.name = 'Uncategorized';";
            
            sqlite3_exec(db, migrate_sql, NULL, NULL, NULL);
            
            printf("Migration complete.\n");
        }
    }
    
    return 0;
}

void close_database(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

/* ============================================
 * Statistics
 * ============================================ */

void show_stats(void) {
    sqlite3_stmt *stmt;
    
    printf("\n[Database Statistics]\n");
    
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM paths;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Total paths:  %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM paths WHERE is_directory = 1;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Directories:  %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM paths WHERE is_directory = 0;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  Files:        %d\n", sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    /* Count tags */
    int total_tags = 0, tags_in_use = 0;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM tags;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total_tags = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    sqlite3_prepare_v2(db, "SELECT COUNT(DISTINCT tag_id) FROM path_tags;", -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        tags_in_use = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    printf("  Tags:         %d (%d in use)\n", total_tags, tags_in_use);
    
    /* Show unused tags hint */
    int unused_tags = total_tags - tags_in_use;
    if (unused_tags > 0) {
        printf("                (%d unused, run 'prune-tags' to remove)\n", unused_tags);
    }
    
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
