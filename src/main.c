/*
 * FileSearch - Main Entry Point and CLI
 * 
 * Lightweight file/folder path management system with tagging and search.
 * 
 * Features:
 * - Persistent SQLite database storage
 * - Categories and tags (many-to-many relationships)
 * - Search by name, tags, categories (exact, prefix, substring, fuzzy)
 * - Structured search with --category, --tag, --tag-fuzzy, --name flags
 * - Depth-controlled directory scanning
 * - Tag management (create, delete, rename, prune)
 * - Database-stored settings with schema versioning
 * - Cross-platform support (Windows/macOS/Linux)
 * 
 * Compile:
 *   make
 *   or: gcc -o filesearch main.c utils.c database.c paths.c tags.c categories.c search.c -lsqlite3
 * 
 * Usage:
 *   ./filesearch [--db /path/to/database.db]
 */

#include "utils.h"
#include "database.h"
#include "paths.h"
#include "tags.h"
#include "categories.h"
#include "search.h"

/* ============================================
 * CLI Help
 * ============================================ */

void print_help(void) {
    printf("\n");
    printf("Path Commands:\n");
    printf("  add <directory> [-d N]        - Add directory to database\n");
    printf("                                  -d N limits recursion depth (0=no recurse)\n");
    printf("  remove <path>                 - Remove path from database\n");
    printf("  info <path>                   - Show path details with tags and categories\n");
    printf("\n");
    printf("Search Commands:\n");
    printf("  search <term>                 - Search paths by name (all methods)\n");
    printf("  exact <term>                  - Exact match on path names\n");
    printf("  prefix <term>                 - Prefix match on path names\n");
    printf("  substring <term>              - Substring match on path names\n");
    printf("  fuzzy <term> [n]              - Fuzzy match with max distance n\n");
    printf("  find [options]                - Structured search with filters:\n");
    printf("       --category, -c <name>      Filter by category (exact)\n");
    printf("       --tag, -t <name>           Filter by tag (exact/substring)\n");
    printf("       --tag-fuzzy, -tf <name>    Filter by tag (fuzzy match)\n");
    printf("       --name, -n <term>          Filter by path name (substring)\n");
    printf("       --fuzzy-distance, -fd <n>  Set fuzzy distance (default: 3)\n");
    printf("\n");
    printf("Tag Commands:\n");
    printf("  tag <path> <tagname>          - Add tag to path\n");
    printf("  untag <path> <tagname>        - Remove tag from path\n");
    printf("  tags [path]                   - List all tags, or tags on a path\n");
    printf("  tagsearch <term>              - Search existing tags\n");
    printf("  delete-tag <name>             - Delete a tag completely\n");
    printf("  rename-tag <old> <new>        - Rename a tag\n");
    printf("  prune-tags                    - Remove all unused tags\n");
    printf("\n");
    printf("Category Commands:\n");
    printf("  categorize <path> <category>  - Add category to path\n");
    printf("  uncategorize <path> <category>- Remove category from path\n");
    printf("  categories [path]             - List all categories, or categories on a path\n");
    printf("  create-category <name>        - Create new category\n");
    printf("  delete-category <name>        - Delete a category\n");
    printf("  rename-category <old> <new>   - Rename a category\n");
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

/* ============================================
 * CLI Command Parsing
 * ============================================ */

/*
 * Parse 'add' command arguments for optional depth flag.
 * Returns max_depth (-1 for unlimited, or specified value).
 */
int parse_add_args(const char *args, char *path, size_t path_size) {
    int max_depth = get_int_setting("default_scan_depth", DEFAULT_SCAN_DEPTH);
    
    path[0] = '\0';
    
    char args_copy[MAX_INPUT_LENGTH];
    strncpy(args_copy, args, sizeof(args_copy) - 1);
    args_copy[sizeof(args_copy) - 1] = '\0';
    
    /* Look for -d flag */
    char *depth_flag = strstr(args_copy, " -d ");
    if (!depth_flag) {
        depth_flag = strstr(args_copy, " -d");
        if (depth_flag && depth_flag[3] == '\0') {
            /* -d at end with no value - invalid */
            depth_flag = NULL;
        }
    }
    
    if (depth_flag) {
        /* Parse depth value */
        char *value_start = depth_flag + 4;
        while (*value_start == ' ') value_start++;
        max_depth = atoi(value_start);
        if (max_depth < 0) max_depth = 0;
        
        /* Remove -d and value from path */
        *depth_flag = '\0';
    }
    
    /* Copy remaining as path */
    strncpy(path, args_copy, path_size - 1);
    path[path_size - 1] = '\0';
    trim_whitespace(path);
    
    return max_depth;
}

/* ============================================
 * Interactive CLI
 * ============================================ */

void run_interactive_cli(void) {
    char input[MAX_INPUT_LENGTH];
    char command[64];
    char argument[MAX_INPUT_LENGTH];
    
    printf("\nFileSearch v%d - Interactive CLI\n", 
           get_int_setting("app_version", DEFAULT_APP_VERSION));
    printf("Type 'help' for available commands.\n\n");
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        if (!read_utf8_line(input, sizeof(input), stdin)) {
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
            
            strncpy(argument, space + 1, sizeof(argument) - 1);
            argument[sizeof(argument) - 1] = '\0';
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
                printf("Usage: add <directory> [-d depth]\n");
                printf("  -d 0   Add directory only (no contents)\n");
                printf("  -d 1   Add immediate children only\n");
                printf("  -d N   Recurse N levels deep\n");
                printf("  (omit -d for unlimited recursion)\n");
            } else {
                char path[MAX_PATH_LENGTH];
                int depth = parse_add_args(argument, path, sizeof(path));
                if (strlen(path) > 0) {
                    add_directory(path, depth);
                } else {
                    printf("Usage: add <directory> [-d depth]\n");
                }
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
                printf("Usage: find [options]\n");
                printf("  --category, -c <name>      Filter by category\n");
                printf("  --tag, -t <name>           Filter by tag (exact/substring)\n");
                printf("  --tag-fuzzy, -tf <name>    Filter by tag (fuzzy)\n");
                printf("  --name, -n <term>          Filter by path name\n");
                printf("  --fuzzy-distance, -fd <n>  Fuzzy distance (default: 3)\n");
            } else {
                SearchFilters filters;
                parse_find_args(argument, &filters);
                
                if (filters.category[0] == '\0' && filters.tag[0] == '\0' && 
                    filters.name[0] == '\0') {
                    printf("At least one filter is required.\n");
                } else {
                    structured_search(&filters);
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
                search_tags_all(argument);
            }
        }
        else if (strcmp(command, "delete-tag") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: delete-tag <name>\n");
            } else {
                delete_tag(argument);
            }
        }
        else if (strcmp(command, "rename-tag") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: rename-tag <old_name> <new_name>\n");
            } else {
                char old_name[MAX_TAG_LENGTH], new_name[MAX_TAG_LENGTH];
                if (sscanf(argument, "%255s %255s", old_name, new_name) == 2) {
                    rename_tag(old_name, new_name);
                } else {
                    printf("Usage: rename-tag <old_name> <new_name>\n");
                }
            }
        }
        else if (strcmp(command, "prune-tags") == 0) {
            prune_unused_tags();
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
        else if (strcmp(command, "delete-category") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: delete-category <name>\n");
            } else {
                delete_category(argument);
            }
        }
        else if (strcmp(command, "rename-category") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: rename-category <old_name> <new_name>\n");
            } else {
                char old_name[256], new_name[256];
                if (sscanf(argument, "%255s %255s", old_name, new_name) == 2) {
                    rename_category(old_name, new_name);
                } else {
                    printf("Usage: rename-category <old_name> <new_name>\n");
                }
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
 * Main Entry Point
 * ============================================ */

int main(int argc, char *argv[]) {
    char db_path[MAX_PATH_LENGTH];
    int custom_db = 0;
    
    /* Initialize UTF-8 support */
    init_utf8_support();
    
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
    close_database();
    return 0;
}
