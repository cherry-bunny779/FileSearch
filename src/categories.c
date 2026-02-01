/*
 * FileSearch - Category Operations Implementation
 */

#include "categories.h"
#include "database.h"

/* Forward declaration for get_path_id */
int get_path_id(const char *path);

/* ============================================
 * Category ID Lookup
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

/* ============================================
 * Category CRUD Operations
 * ============================================ */

int create_category(const char *name) {
    /* Check if already exists */
    if (get_category_id(name) >= 0) {
        fprintf(stderr, "Category '%s' already exists.\n", name);
        return -1;
    }
    
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
    
    fprintf(stderr, "Failed to create category.\n");
    return -1;
}

int delete_category(const char *name) {
    int category_id = get_category_id(name);
    if (category_id < 0) {
        fprintf(stderr, "Category not found: %s\n", name);
        return -1;
    }
    
    /* Check usage count */
    sqlite3_stmt *stmt;
    const char *count_sql = "SELECT COUNT(*) FROM path_categories WHERE category_id = ?;";
    int usage = 0;
    
    if (sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, category_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            usage = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    if (usage > 0) {
        printf("Category '%s' is assigned to %d path(s).\n", name, usage);
        if (!get_confirmation("Delete category and remove all associations?")) {
            printf("Cancelled.\n");
            return -1;
        }
    }
    
    /* Delete from path_categories first */
    const char *sql1 = "DELETE FROM path_categories WHERE category_id = ?;";
    
    if (sqlite3_prepare_v2(db, sql1, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, category_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    /* Delete the category itself */
    const char *sql2 = "DELETE FROM categories WHERE id = ?;";
    
    if (sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, category_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        printf("Deleted category: %s\n", name);
        return 0;
    }
    return -1;
}

int rename_category(const char *old_name, const char *new_name) {
    int category_id = get_category_id(old_name);
    if (category_id < 0) {
        fprintf(stderr, "Category not found: %s\n", old_name);
        return -1;
    }
    
    /* Check if new name already exists */
    int existing_id = get_category_id(new_name);
    if (existing_id >= 0) {
        fprintf(stderr, "Category '%s' already exists.\n", new_name);
        return -1;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE categories SET name = ? WHERE id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, new_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, category_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        printf("Renamed category: '%s' -> '%s'\n", old_name, new_name);
        return 0;
    }
    return -1;
}

/* ============================================
 * Path-Category Associations
 * ============================================ */

int categorize_path(const char *path, const char *category_name) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        printf_utf8("Path not found in database: %s\n", path);
        return -1;
    }
    
    int category_id = get_category_id(category_name);
    if (category_id < 0) {
        printf_utf8("Category not found: %s\n", category_name);
        printf_utf8("Use 'create-category %s' to create it first.\n", category_name);
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
        printf_utf8("Categorized: %s [%s]\n", path, category_name);
        
        /* If assigning a category other than "Uncategorized", remove from Uncategorized */
        /* Case-insensitive comparison */
        char lower_name[MAX_TAG_LENGTH];
        strncpy(lower_name, category_name, sizeof(lower_name) - 1);
        lower_name[sizeof(lower_name) - 1] = '\0';
        str_to_lower(lower_name);
        
        if (strcmp(lower_name, "uncategorized") != 0) {
            remove_uncategorized(path_id);
        }
        
        return 0;
    }
    return -1;
}

int uncategorize_path(const char *path, const char *category_name) {
    int path_id = get_path_id(path);
    if (path_id < 0) {
        printf_utf8("Path not found in database: %s\n", path);
        return -1;
    }
    
    int category_id = get_category_id(category_name);
    if (category_id < 0) {
        printf_utf8("Category not found: %s\n", category_name);
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
        printf_utf8("Uncategorized: %s [%s]\n", path, category_name);
        return 0;
    }
    return -1;
}

/* ============================================
 * Internal Categorization (Silent)
 * ============================================ */

/*
 * Categorize a path by ID (no output).
 * Used internally for auto-categorization.
 */
int categorize_path_by_id(int path_id, int category_id) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR IGNORE INTO path_categories (path_id, category_id) VALUES (?, ?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    sqlite3_bind_int(stmt, 2, category_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/*
 * Remove category from path by IDs. Silent operation with no console output.
 * Used internally for bulk operations.
 */
int uncategorize_path_by_id(int path_id, int category_id) {
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM path_categories WHERE path_id = ? AND category_id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    sqlite3_bind_int(stmt, 2, category_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/*
 * Check if a path has any category assigned.
 */
int is_categorized(int path_id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT 1 FROM path_categories WHERE path_id = ? LIMIT 1;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    
    int result = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    return result;
}

/*
 * Assign "Uncategorized" category to a path by ID.
 * Used when adding new paths to the database.
 */
int assign_uncategorized(int path_id) {
    int uncat_id = get_category_id("Uncategorized");
    if (uncat_id < 0) {
        /* Uncategorized category doesn't exist, create it */
        uncat_id = create_category("Uncategorized");
        if (uncat_id < 0) {
            return -1;
        }
    }
    
    return categorize_path_by_id(path_id, uncat_id);
}

/*
 * Remove "Uncategorized" category from a path by ID.
 * Used when assigning a real category to a path.
 */
int remove_uncategorized(int path_id) {
    int uncat_id = get_category_id("Uncategorized");
    if (uncat_id < 0) {
        return 0;  /* No Uncategorized category exists, nothing to remove */
    }
    
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM path_categories WHERE path_id = ? AND category_id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, path_id);
    sqlite3_bind_int(stmt, 2, uncat_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ============================================
 * Category Listing
 * ============================================ */

void list_all_categories(void) {
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT c.name, COUNT(pc.path_id) as usage "
        "FROM categories c LEFT JOIN path_categories pc ON c.id = pc.category_id "
        "GROUP BY c.id ORDER BY c.name;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    printf("\n[All Categories]\n");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        int usage = sqlite3_column_int(stmt, 1);
        printf("  %-30s (%d)\n", name, usage);
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
