/*
 * FileSearch - Tag Operations Implementation
 */

#include "tags.h"
#include "database.h"

/* Forward declaration for get_path_id */
int get_path_id(const char *path);

/* ============================================
 * Tag ID Lookup
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

/* ============================================
 * Tag CRUD Operations
 * ============================================ */

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

int delete_tag(const char *name) {
    int tag_id = get_tag_id(name);
    if (tag_id < 0) {
        fprintf(stderr, "Tag not found: %s\n", name);
        return -1;
    }
    
    /* Check usage count */
    int usage = get_tag_usage_count(name);
    if (usage > 0) {
        printf("Tag '%s' is assigned to %d path(s).\n", name, usage);
        if (!get_confirmation("Delete tag and remove all associations?")) {
            printf("Cancelled.\n");
            return -1;
        }
    }
    
    /* Delete from path_tags first (cascade should handle this, but be explicit) */
    sqlite3_stmt *stmt;
    const char *sql1 = "DELETE FROM path_tags WHERE tag_id = ?;";
    
    if (sqlite3_prepare_v2(db, sql1, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, tag_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    /* Delete the tag itself */
    const char *sql2 = "DELETE FROM tags WHERE id = ?;";
    
    if (sqlite3_prepare_v2(db, sql2, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, tag_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        printf("Deleted tag: %s\n", name);
        return 0;
    }
    return -1;
}

int rename_tag(const char *old_name, const char *new_name) {
    int tag_id = get_tag_id(old_name);
    if (tag_id < 0) {
        fprintf(stderr, "Tag not found: %s\n", old_name);
        return -1;
    }
    
    /* Check if new name already exists */
    int existing_id = get_tag_id(new_name);
    if (existing_id >= 0) {
        fprintf(stderr, "Tag '%s' already exists.\n", new_name);
        return -1;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE tags SET name = ? WHERE id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, new_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, tag_id);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        printf("Renamed tag: '%s' -> '%s'\n", old_name, new_name);
        return 0;
    }
    return -1;
}

int prune_unused_tags(void) {
    sqlite3_stmt *stmt;
    
    /* First, list unused tags */
    const char *list_sql = 
        "SELECT name FROM tags WHERE id NOT IN (SELECT DISTINCT tag_id FROM path_tags) ORDER BY name;";
    
    if (sqlite3_prepare_v2(db, list_sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    printf("\n[Unused Tags]\n");
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  %s\n", sqlite3_column_text(stmt, 0));
        count++;
    }
    sqlite3_finalize(stmt);
    
    if (count == 0) {
        printf("  (no unused tags)\n\n");
        return 0;
    }
    
    printf("\nFound %d unused tag(s).\n", count);
    
    if (!get_confirmation("Delete all unused tags?")) {
        printf("Cancelled.\n");
        return -1;
    }
    
    /* Delete unused tags */
    const char *delete_sql = 
        "DELETE FROM tags WHERE id NOT IN (SELECT DISTINCT tag_id FROM path_tags);";
    
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, delete_sql, NULL, NULL, &err_msg);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Delete error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    printf("Deleted %d unused tag(s).\n", count);
    return count;
}

/* ============================================
 * Tag Usage Count
 * ============================================ */

int get_tag_usage_count(const char *name) {
    int tag_id = get_tag_id(name);
    if (tag_id < 0) {
        return 0;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM path_tags WHERE tag_id = ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, tag_id);
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

/* ============================================
 * Tag Similarity Checking
 * ============================================ */

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

int get_or_create_tag_with_check(const char *tag_name) {
    /* Check if exact tag exists */
    int tag_id = get_tag_id(tag_name);
    if (tag_id >= 0) {
        return tag_id;
    }
    
    /* Check for similar existing tags */
    char similar_name[MAX_TAG_LENGTH];
    int similar_distance;
    int is_substr;
    
    if (find_similar_tags(tag_name, similar_name, sizeof(similar_name), 
                          &similar_distance, &is_substr) > 0) {
        if (is_substr) {
            printf("Warning: Similar tag exists: '%s' (substring match)\n", similar_name);
        } else {
            printf("Warning: Similar tag exists: '%s' (distance: %d)\n", 
                   similar_name, similar_distance);
        }
        
        char prompt[384];
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

/* ============================================
 * Path-Tag Associations
 * ============================================ */

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
            
            /* Get actual tag name */
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

/* ============================================
 * Tag Listing
 * ============================================ */

void list_all_tags(void) {
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT t.name, COUNT(pt.path_id) as usage "
        "FROM tags t LEFT JOIN path_tags pt ON t.id = pt.tag_id "
        "GROUP BY t.id ORDER BY t.name;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    printf("\n[All Tags]\n");
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        int usage = sqlite3_column_int(stmt, 1);
        printf("  %-30s (%d)\n", name, usage);
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

/* ============================================
 * Tag Searching
 * ============================================ */

void search_tags_all(const char *query) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    int fuzzy_dist = get_int_setting("fuzzy_default_distance", DEFAULT_FUZZY_DISTANCE);
    
    sqlite3_stmt *stmt;
    
    /* Exact match */
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
