/*
 * FileSearch - Search Functions Implementation
 */

#include "search.h"
#include "database.h"
#include "tags.h"

/* ============================================
 * Result Display Helper
 * ============================================ */

static void print_path_result(sqlite3_stmt *stmt, int show_distance) {
    const char *path = (const char *)sqlite3_column_text(stmt, 0);
    int is_dir = sqlite3_column_int(stmt, 1);
    
    if (is_dir) {
        printf_utf8("  [DIR]  %s", path);
    } else {
        long long size = sqlite3_column_int64(stmt, 2);
        printf_utf8("  [FILE] %s (%lld bytes)", path, size);
    }
    
    if (show_distance) {
        int dist = sqlite3_column_int(stmt, 3);
        printf_utf8(" (distance: %d)", dist);
    }
    
    printf_utf8("\n");
}

/* ============================================
 * Path Search by Name
 * ============================================ */

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
 * Full Path Search (searches 'path' column)
 * ============================================ */

void search_fullpath_exact(const char *query) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE path = ? COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_results);
    
    printf("\n[Exact Match - Full Path]\n");
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

void search_fullpath_prefix(const char *query) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE path LIKE ? || '%' COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_results);
    
    printf("\n[Prefix Match - Full Path]\n");
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

void search_fullpath_substring(const char *query) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size FROM paths "
        "WHERE path LIKE '%' || ? || '%' COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_results);
    
    printf("\n[Substring Match - Full Path]\n");
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

void search_fullpath_fuzzy(const char *query, int max_distance) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    if (max_distance < 0) {
        max_distance = get_int_setting("fuzzy_default_distance", DEFAULT_FUZZY_DISTANCE);
    }
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT path, is_directory, size, levenshtein(path, ?) as dist "
        "FROM paths WHERE dist <= ? ORDER BY dist, path LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_distance);
    sqlite3_bind_int(stmt, 3, max_results);
    
    printf("\n[Fuzzy Match - Full Path (distance <= %d)]\n", max_distance);
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

void search_fullpath_all(const char *query) {
    search_fullpath_exact(query);
    search_fullpath_prefix(query);
    search_fullpath_substring(query);
    search_fullpath_fuzzy(query, -1);
}

/* ============================================
 * Find Matching Tags (Helper for Fuzzy Search)
 * ============================================ */

/*
 * Find tags matching the query using the specified search mode.
 * Returns a comma-separated list of matching tag IDs, or NULL if none found.
 * Caller must free the returned string.
 */
static char *find_matching_tag_ids(const char *tag_query, int fuzzy, int fuzzy_distance) {
    sqlite3_stmt *stmt;
    char *result = NULL;
    size_t result_size = 0;
    size_t result_capacity = 256;
    
    result = malloc(result_capacity);
    if (!result) return NULL;
    result[0] = '\0';
    
    const char *sql;
    
    if (fuzzy) {
        /* Fuzzy match */
        sql = "SELECT id, name, levenshtein(name, ?) as dist FROM tags "
              "WHERE dist <= ? ORDER BY dist;";
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            free(result);
            return NULL;
        }
        
        sqlite3_bind_text(stmt, 1, tag_query, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, fuzzy_distance);
    } else {
        /* Exact or substring match */
        /* First try exact, then substring */
        int tag_id = get_tag_id(tag_query);
        
        if (tag_id >= 0) {
            /* Exact match found */
            snprintf(result, result_capacity, "%d", tag_id);
            return result;
        }
        
        /* Try substring match */
        sql = "SELECT id FROM tags WHERE name LIKE '%' || ? || '%' COLLATE NOCASE;";
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            free(result);
            return NULL;
        }
        
        sqlite3_bind_text(stmt, 1, tag_query, -1, SQLITE_STATIC);
    }
    
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        char id_str[32];
        snprintf(id_str, sizeof(id_str), "%s%d", first ? "" : ",", id);
        
        size_t needed = result_size + strlen(id_str) + 1;
        if (needed > result_capacity) {
            result_capacity *= 2;
            char *new_result = realloc(result, result_capacity);
            if (!new_result) {
                sqlite3_finalize(stmt);
                free(result);
                return NULL;
            }
            result = new_result;
        }
        
        strcat(result, id_str);
        result_size = strlen(result);
        first = 0;
    }
    
    sqlite3_finalize(stmt);
    
    if (result[0] == '\0') {
        free(result);
        return NULL;
    }
    
    return result;
}

/* ============================================
 * Structured Search
 * ============================================ */

void structured_search(const SearchFilters *filters) {
    int max_results = get_int_setting("max_results", DEFAULT_MAX_RESULTS);
    
    /* Handle fuzzy tag search - find matching tag IDs first */
    char *tag_ids = NULL;
    int tag_match_count = 0;
    
    if (filters->tag[0] != '\0') {
        tag_ids = find_matching_tag_ids(filters->tag, filters->tag_fuzzy, 
                                         filters->tag_fuzzy_distance);
        
        if (!tag_ids) {
            printf("\n[Search Results]\n");
            if (filters->tag_fuzzy) {
                printf("  (no tags found matching '%s' within distance %d)\n\n", 
                       filters->tag, filters->tag_fuzzy_distance);
            } else {
                printf("  (no tags found matching '%s')\n\n", filters->tag);
            }
            return;
        }
        
        /* Count matching tags for display */
        for (const char *p = tag_ids; *p; p++) {
            if (*p == ',') tag_match_count++;
        }
        tag_match_count++;  /* Add 1 for first/only ID */
        
        if (tag_match_count > 1) {
            printf("Found %d tags matching '%s'\n", tag_match_count, filters->tag);
        }
    }
    
    /* Build dynamic SQL based on provided filters */
    char sql[4096];
    int sql_len = 0;
    
    sql_len = snprintf(sql, sizeof(sql), 
        "SELECT DISTINCT p.path, p.is_directory, p.size FROM paths p ");
    
    if (filters->category[0] != '\0') {
        sql_len += snprintf(sql + sql_len, sizeof(sql) - sql_len,
            "JOIN path_categories pc ON p.id = pc.path_id "
            "JOIN categories c ON pc.category_id = c.id ");
    }
    
    if (tag_ids) {
        sql_len += snprintf(sql + sql_len, sizeof(sql) - sql_len,
            "JOIN path_tags pt ON p.id = pt.path_id ");
    }
    
    int has_where = 0;
    
    if (filters->category[0] != '\0') {
        sql_len += snprintf(sql + sql_len, sizeof(sql) - sql_len,
            "WHERE c.name = ?1 COLLATE NOCASE ");
        has_where = 1;
    }
    
    if (tag_ids) {
        sql_len += snprintf(sql + sql_len, sizeof(sql) - sql_len,
            "%s pt.tag_id IN (%s) ", has_where ? "AND" : "WHERE", tag_ids);
        has_where = 1;
    }
    
    if (filters->name[0] != '\0') {
        sql_len += snprintf(sql + sql_len, sizeof(sql) - sql_len,
            "%s p.name LIKE '%%' || ?2 || '%%' COLLATE NOCASE ", 
            has_where ? "AND" : "WHERE");
        has_where = 1;
    }
    
    snprintf(sql + sql_len, sizeof(sql) - sql_len,
        "ORDER BY p.path LIMIT ?3;");
    
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        free(tag_ids);
        return;
    }
    
    /* Bind parameters by name */
    if (filters->category[0] != '\0') {
        sqlite3_bind_text(stmt, 1, filters->category, -1, SQLITE_STATIC);
    }
    if (filters->name[0] != '\0') {
        sqlite3_bind_text(stmt, 2, filters->name, -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, 3, max_results);
    
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
    free(tag_ids);
}

/* ============================================
 * Parse Find Arguments
 * ============================================ */

void parse_find_args(const char *args, SearchFilters *filters) {
    /* Initialize filters */
    memset(filters, 0, sizeof(SearchFilters));
    filters->tag_fuzzy_distance = get_int_setting("fuzzy_default_distance", DEFAULT_FUZZY_DISTANCE);
    
    char args_copy[MAX_INPUT_LENGTH];
    strncpy(args_copy, args, sizeof(args_copy) - 1);
    args_copy[sizeof(args_copy) - 1] = '\0';
    
    char *token = strtok(args_copy, " ");
    while (token) {
        if (strcmp(token, "--category") == 0 || strcmp(token, "-c") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                strncpy(filters->category, token, sizeof(filters->category) - 1);
            }
        } 
        else if (strcmp(token, "--tag") == 0 || strcmp(token, "-t") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                strncpy(filters->tag, token, sizeof(filters->tag) - 1);
            }
        }
        else if (strcmp(token, "--tag-fuzzy") == 0 || strcmp(token, "-tf") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                strncpy(filters->tag, token, sizeof(filters->tag) - 1);
                filters->tag_fuzzy = 1;
            }
        }
        else if (strcmp(token, "--name") == 0 || strcmp(token, "-n") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                strncpy(filters->name, token, sizeof(filters->name) - 1);
            }
        }
        else if (strcmp(token, "--fuzzy-distance") == 0 || strcmp(token, "-fd") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                filters->tag_fuzzy_distance = atoi(token);
                if (filters->tag_fuzzy_distance < 1) filters->tag_fuzzy_distance = 1;
                if (filters->tag_fuzzy_distance > 10) filters->tag_fuzzy_distance = 10;
            }
        }
        
        token = strtok(NULL, " ");
    }
}
