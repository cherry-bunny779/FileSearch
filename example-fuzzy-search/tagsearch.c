/*
 * Tag Search Demo with Fuzzy Matching
 * 
 * Features:
 * - Loads tags from a text file into SQLite
 * - Exact, prefix, substring, and fuzzy (Levenshtein) search
 * - Interactive command-line interface
 * 
 * Usage:   ./tagsearch tags.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../deps/sqlite3.h"

#define MAX_TAG_LENGTH 256
#define MAX_INPUT_LENGTH 512
#define MAX_RESULTS 20

/* ============================================
 * Levenshtein Distance Implementation
 * ============================================ */

int min3(int a, int b, int c) {
    int min = a;
    if (b < min) min = b;
    if (c < min) min = c;
    return min;
}

/*
 * Calculate Levenshtein distance between two strings.
 * Uses O(min(m,n)) space with two-row optimization.
 */
int levenshtein(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    // Handle edge cases
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    // Ensure s1 is the shorter string for space optimization
    if (len1 > len2) {
        const char *temp = s1;
        s1 = s2;
        s2 = temp;
        int t = len1;
        len1 = len2;
        len2 = t;
    }
    
    // Allocate two rows
    int *prev = malloc((len1 + 1) * sizeof(int));
    int *curr = malloc((len1 + 1) * sizeof(int));
    
    if (!prev || !curr) {
        free(prev);
        free(curr);
        return -1;
    }
    
    // Initialize first row
    for (int i = 0; i <= len1; i++) {
        prev[i] = i;
    }
    
    // Fill the matrix row by row
    for (int j = 1; j <= len2; j++) {
        curr[0] = j;
        
        for (int i = 1; i <= len1; i++) {
            int cost = (tolower(s1[i-1]) == tolower(s2[j-1])) ? 0 : 1;
            
            curr[i] = min3(
                prev[i] + 1,      // deletion
                curr[i-1] + 1,    // insertion
                prev[i-1] + cost  // substitution
            );
        }
        
        // Swap rows
        int *temp = prev;
        prev = curr;
        curr = temp;
    }
    
    int result = prev[len1];
    free(prev);
    free(curr);
    
    return result;
}

/* ============================================
 * SQLite Custom Function Registration
 * ============================================ */

/*
 * SQLite wrapper for Levenshtein distance.
 * Usage in SQL: SELECT levenshtein('string1', 'string2');
 */
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
    
    int distance = levenshtein(s1, s2);
    sqlite3_result_int(ctx, distance);
}

/* ============================================
 * Database Operations
 * ============================================ */

sqlite3 *db = NULL;

int init_database() {
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    // Register custom Levenshtein function
    rc = sqlite3_create_function(db, "levenshtein", 2, SQLITE_UTF8, NULL,
                                  sqlite_levenshtein, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot register function: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    // Create tags table
    const char *sql = 
        "CREATE TABLE tags ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT UNIQUE NOT NULL"
        ");"
        "CREATE INDEX idx_tag_name ON tags(name);";
    
    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    return 0;
}

int load_tags_from_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return -1;
    }
    
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR IGNORE INTO tags (name) VALUES (?);";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
        fclose(file);
        return -1;
    }
    
    // Begin transaction for faster inserts
    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
    char line[MAX_TAG_LENGTH];
    int count = 0;
    
    while (fgets(line, sizeof(line), file)) {
        // Trim newline and whitespace
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || 
                          line[len-1] == ' ' || line[len-1] == '\t')) {
            line[--len] = '\0';
        }
        
        // Skip empty lines
        if (len == 0) continue;
        
        sqlite3_bind_text(stmt, 1, line, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "Insert failed for '%s': %s\n", line, sqlite3_errmsg(db));
        } else {
            count++;
        }
        
        sqlite3_reset(stmt);
    }
    
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_finalize(stmt);
    fclose(file);
    
    printf("Loaded %d tags from '%s'\n\n", count, filename);
    return count;
}

/* ============================================
 * Search Functions
 * ============================================ */

void search_exact(const char *query) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM tags WHERE name = ? COLLATE NOCASE;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    
    printf("\n[Exact Match]\n");
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

void search_prefix(const char *query) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM tags WHERE name LIKE ? || '%' COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, MAX_RESULTS);
    
    printf("\n[Prefix Match]\n");
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("  %s\n", sqlite3_column_text(stmt, 0));
        found++;
    }
    
    if (!found) {
        printf("  (no prefix matches)\n");
    }
    
    sqlite3_finalize(stmt);
}

void search_substring(const char *query) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM tags WHERE name LIKE '%' || ? || '%' COLLATE NOCASE LIMIT ?;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Query error: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, MAX_RESULTS);
    
    printf("\n[Substring Match]\n");
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

void search_fuzzy(const char *query, int max_distance) {
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
    
    printf("\n[Fuzzy Match (distance <= %d)]\n", max_distance);
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

void search_all(const char *query) {
    search_exact(query);
    search_prefix(query);
    search_substring(query);
    search_fuzzy(query, 2);
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
 * Interactive CLI
 * ============================================ */

void trim_whitespace(char *str) {
    // Trim leading
    char *start = str;
    while (*start && isspace(*start)) start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    // Trim trailing
    size_t len = strlen(str);
    while (len > 0 && isspace(str[len - 1])) {
        str[--len] = '\0';
    }
}

void print_help() {
    printf("\n");
    printf("Commands:\n");
    printf("  search <term>     - Search using all methods (exact, prefix, substring, fuzzy)\n");
    printf("  exact <term>      - Exact match only\n");
    printf("  prefix <term>     - Prefix match (autocomplete style)\n");
    printf("  substring <term>  - Substring match (contains)\n");
    printf("  fuzzy <term> [n]  - Fuzzy match with max distance n (default: 2)\n");
    printf("  list              - List all tags\n");
    printf("  help              - Show this help\n");
    printf("  quit / exit       - Exit the program\n");
    printf("\n");
    printf("Examples:\n");
    printf("  search finanse    - Find tags similar to 'finanse' (typo for 'finance')\n");
    printf("  prefix pro        - Find tags starting with 'pro'\n");
    printf("  fuzzy urjent 1    - Find tags within edit distance 1 of 'urjent'\n");
    printf("\n");
}

void run_interactive_cli() {
    char input[MAX_INPUT_LENGTH];
    char command[64];
    char argument[MAX_INPUT_LENGTH];
    
    printf("Tag Search Demo - Interactive CLI\n");
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
        else if (strcmp(command, "list") == 0) {
            list_all_tags();
        }
        else if (strcmp(command, "search") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: search <term>\n");
            } else {
                search_all(argument);
            }
        }
        else if (strcmp(command, "exact") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: exact <term>\n");
            } else {
                search_exact(argument);
            }
        }
        else if (strcmp(command, "prefix") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: prefix <term>\n");
            } else {
                search_prefix(argument);
            }
        }
        else if (strcmp(command, "substring") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: substring <term>\n");
            } else {
                search_substring(argument);
            }
        }
        else if (strcmp(command, "fuzzy") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: fuzzy <term> [max_distance]\n");
            } else {
                // Parse optional distance parameter
                char term[MAX_TAG_LENGTH];
                int distance = 2;
                
                if (sscanf(argument, "%255s %d", term, &distance) < 1) {
                    printf("Usage: fuzzy <term> [max_distance]\n");
                } else {
                    if (distance < 0) distance = 0;
                    if (distance > 10) distance = 10;
                    search_fuzzy(term, distance);
                }
            }
        }
        else {
            printf("Unknown command: '%s'. Type 'help' for available commands.\n", command);
        }
    }
}

/* ============================================
 * Main
 * ============================================ */

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <tags_file>\n", argv[0]);
        printf("Example: %s tags.txt\n", argv[0]);
        return 1;
    }
    
    // Initialize database
    if (init_database() != 0) {
        return 1;
    }
    
    // Load tags from file
    if (load_tags_from_file(argv[1]) < 0) {
        sqlite3_close(db);
        return 1;
    }
    
    // Run interactive CLI
    run_interactive_cli();
    
    // Cleanup
    sqlite3_close(db);
    return 0;
}