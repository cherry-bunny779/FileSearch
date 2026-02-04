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
 * - Command history with readline/libedit (macOS/Linux)
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

/* Readline support for command history and line editing */
#ifdef USE_READLINE
#ifdef USE_EDITLINE
/* macOS with system libedit - use editline header */
#include <editline/readline.h>
#else
/* GNU readline (Linux, or macOS with Homebrew readline) */
#include <readline/readline.h>
#include <readline/history.h>
#endif
#endif

/* ============================================
 * CLI Help
 * ============================================ */

void print_help(void) {
    printf("\n");
    printf("Note: Use quotes for paths with spaces, e.g., \"C:\\Program Files\\App\"\n");
    printf("\n");
    printf("Path Commands:\n");
    printf("  add <path> [-d N] [-c cat...] [-t tag...] [-a]\n");
    printf("                                - Add file or directory to database\n");
    printf("                                  -d N limits recursion depth (0=no recurse)\n");
    printf("                                  -c assigns categories (must exist)\n");
    printf("                                  -t assigns tags (created if needed)\n");
    printf("                                  -a auto-tag from [tag] patterns in names\n");
    printf("  remove <path>                 - Remove path from database\n");
    printf("  remove-all <directory>        - Remove all contents under directory\n");
    printf("  info <path>                   - Show path details with tags and categories\n");
    printf("\n");
    printf("Search Commands:\n");
    printf("  search <term>                 - Search by filename (stores results)\n");
    printf("  search-path <term>            - Search by full path (stores results)\n");
    printf("  exact <term>                  - Exact match on names\n");
    printf("  prefix <term>                 - Prefix match on names\n");
    printf("  substring <term>              - Substring match on names\n");
    printf("  fuzzy <term> [n]              - Fuzzy match with max distance n\n");
    printf("  find [options]                - Structured search with filters:\n");
    printf("       --category, -c <name>      Filter by category (exact)\n");
    printf("       --tag, -t <name>           Filter by tag (exact/substring)\n");
    printf("       --tag-fuzzy, -tf <name>    Filter by tag (fuzzy match)\n");
    printf("       --name, -n <term>          Filter by path name (substring)\n");
    printf("       --fuzzy-distance, -fd <n>  Set fuzzy distance (default: 3)\n");
    printf("\n");
    printf("Batch Commands (operate on last search results):\n");
    printf("  results                       - Show stored search results\n");
    printf("  clear-results                 - Clear stored results\n");
    printf("  tag-results <tag>             - Add tag to all stored results\n");
    printf("  untag-results <tag>           - Remove tag from all stored results\n");
    printf("  categorize-results <cat>      - Add category to all stored results\n");
    printf("  uncategorize-results <cat>    - Remove category from all stored results\n");
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
    printf("  set-root <category> <path>    - Add root directory for a category\n");
    printf("  unset-root <category> <path>  - Remove root directory from a category\n");
    printf("  roots [category]              - List all roots, or roots for a category\n");
    printf("\n");
    printf("Check/Sync Commands:\n");
    printf("  check <path> [-d N]           - Check path against database (find new/missing)\n");
    printf("  check -c <category>           - Check all roots for a category\n");
    printf("  check -c <category> --root <path>\n");
    printf("                                - Check specific root for a category\n");
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

#define MAX_CATEGORIES 16
#define MAX_TAGS 32

typedef struct {
    char path[MAX_PATH_LENGTH];
    int max_depth;
    char categories[MAX_CATEGORIES][MAX_TAG_LENGTH];
    int category_count;
    char tags[MAX_TAGS][MAX_TAG_LENGTH];
    int tag_count;
    int auto_tag;  /* Extract tags from [tag] patterns in filenames */
} AddArgs;

/*
 * Parse 'add' command arguments.
 * Syntax: add <path> [-d depth] [-c cat1 cat2 ...] [-t tag1 tag2 ...] [-a|--auto-tag]
 * Path can be quoted: add "C:\Program Files\Game" -d 0
 */
void parse_add_args_full(const char *args, AddArgs *result) {
    /* Initialize result */
    result->path[0] = '\0';
    result->max_depth = get_int_setting("default_scan_depth", DEFAULT_SCAN_DEPTH);
    result->category_count = 0;
    result->tag_count = 0;
    result->auto_tag = 0;
    
    if (!args || strlen(args) == 0) {
        return;
    }
    
    /* First, extract the path (possibly quoted) */
    const char *remainder = NULL;
    if (extract_quoted_path(args, result->path, sizeof(result->path), &remainder) != 0) {
        return;  /* Failed to extract path */
    }
    
    /* If no remainder, we're done */
    if (!remainder || *remainder == '\0') {
        return;
    }
    
    /* Skip whitespace */
    while (*remainder && isspace(*remainder)) remainder++;
    if (*remainder == '\0') {
        return;
    }
    
    /* Parse remaining arguments */
    char args_copy[MAX_INPUT_LENGTH * 2];
    strncpy(args_copy, remainder, sizeof(args_copy) - 1);
    args_copy[sizeof(args_copy) - 1] = '\0';
    
    /* Tokenize and parse flags */
    char *tokens[128];
    int token_count = 0;
    
    char *token = strtok(args_copy, " ");
    while (token && token_count < 128) {
        tokens[token_count++] = token;
        token = strtok(NULL, " ");
    }
    
    /* Parse mode: 1=depth, 2=categories, 3=tags */
    int mode = 0;
    
    for (int i = 0; i < token_count; i++) {
        if (strcmp(tokens[i], "-d") == 0) {
            mode = 1;  /* Next token is depth value */
        } else if (strcmp(tokens[i], "-c") == 0) {
            mode = 2;  /* Following tokens are categories until another flag */
        } else if (strcmp(tokens[i], "-t") == 0) {
            mode = 3;  /* Following tokens are tags until another flag */
        } else if (strcmp(tokens[i], "-a") == 0 || strcmp(tokens[i], "--auto-tag") == 0) {
            result->auto_tag = 1;
            mode = 0;
        } else if (tokens[i][0] == '-' && (tokens[i][1] == 'd' || tokens[i][1] == 'c' || tokens[i][1] == 't')) {
            /* Handle combined flags like -d0 */
            if (tokens[i][1] == 'd' && tokens[i][2] != '\0') {
                result->max_depth = atoi(&tokens[i][2]);
                if (result->max_depth < 0) result->max_depth = 0;
                mode = 0;
            }
        } else {
            /* Regular token - interpret based on mode */
            switch (mode) {
                case 1:  /* Depth value */
                    result->max_depth = atoi(tokens[i]);
                    if (result->max_depth < 0) result->max_depth = 0;
                    mode = 0;
                    break;
                case 2:  /* Category */
                    if (result->category_count < MAX_CATEGORIES) {
                        strncpy(result->categories[result->category_count], tokens[i], 
                                MAX_TAG_LENGTH - 1);
                        result->categories[result->category_count][MAX_TAG_LENGTH - 1] = '\0';
                        result->category_count++;
                    }
                    break;
                case 3:  /* Tag */
                    if (result->tag_count < MAX_TAGS) {
                        strncpy(result->tags[result->tag_count], tokens[i], 
                                MAX_TAG_LENGTH - 1);
                        result->tags[result->tag_count][MAX_TAG_LENGTH - 1] = '\0';
                        result->tag_count++;
                    }
                    break;
                default:
                    /* Ignore unexpected tokens */
                    break;
            }
        }
    }
}

/*
 * Legacy wrapper for backward compatibility.
 * Parse 'add' command arguments for optional depth flag.
 * Returns max_depth (-1 for unlimited, or specified value).
 */
int parse_add_args(const char *args, char *path, size_t path_size) {
    AddArgs result;
    parse_add_args_full(args, &result);
    
    strncpy(path, result.path, path_size - 1);
    path[path_size - 1] = '\0';
    
    return result.max_depth;
}

/* ============================================
 * Interactive CLI
 * ============================================ */

#ifdef USE_READLINE
/*
 * Read a line using readline library (macOS/Linux).
 * Provides command history and line editing with arrow keys.
 */
static char *cli_readline(const char *prompt) {
    char *line = readline(prompt);
    if (line && *line) {
        add_history(line);
    }
    return line;
}
#endif

void run_interactive_cli(void) {
    char command[64];
    char argument[MAX_INPUT_LENGTH];
    
    printf("\nFileSearch v%d - Interactive CLI\n", 
           get_int_setting("app_version", DEFAULT_APP_VERSION));
#ifdef USE_READLINE
#ifdef USE_EDITLINE
    printf("Type 'help' for available commands. (editline enabled)\n\n");
#else
    printf("Type 'help' for available commands. (readline enabled)\n\n");
#endif
#else
    printf("Type 'help' for available commands.\n");
    printf("(Tip: Rebuild with 'make' to enable arrow key support)\n\n");
#endif
    
    while (1) {
#ifdef USE_READLINE
        /* Use readline for better line editing on macOS/Linux */
        char *line = cli_readline("> ");
        if (!line) {
            printf("\n");
            break;
        }
        
        char input[MAX_INPUT_LENGTH];
        strncpy(input, line, sizeof(input) - 1);
        input[sizeof(input) - 1] = '\0';
        free(line);
#else
        /* Standard input for Windows or when readline unavailable */
        char input[MAX_INPUT_LENGTH];
        printf("> ");
        fflush(stdout);
        
        if (!read_utf8_line(input, sizeof(input), stdin)) {
            printf("\n");
            break;
        }
#endif
        
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
                printf("Usage: add <path> [-d depth] [-c category...] [-t tag...]\n");
                printf("  For directories:\n");
                printf("    -d 0   Add directory only (no contents)\n");
                printf("    -d 1   Add immediate children only\n");
                printf("    -d N   Recurse N levels deep\n");
                printf("    (omit -d for unlimited recursion)\n");
                printf("  For files:\n");
                printf("    -d flag is ignored\n");
                printf("  Optional:\n");
                printf("    -c Cat1 Cat2 ...  Assign categories (must exist)\n");
                printf("    -t tag1 tag2 ...  Assign tags (created if needed)\n");
                printf("    -a, --auto-tag    Extract tags from [tag] patterns in filenames\n");
            } else {
                AddArgs add_args;
                parse_add_args_full(argument, &add_args);
                
                if (strlen(add_args.path) > 0) {
                    if (add_args.category_count > 0 || add_args.tag_count > 0 || add_args.auto_tag) {
                        /* Convert arrays to pointer arrays for the function call */
                        const char *cat_ptrs[MAX_CATEGORIES];
                        const char *tag_ptrs[MAX_TAGS];
                        
                        for (int i = 0; i < add_args.category_count; i++) {
                            cat_ptrs[i] = add_args.categories[i];
                        }
                        for (int i = 0; i < add_args.tag_count; i++) {
                            tag_ptrs[i] = add_args.tags[i];
                        }
                        
                        add_path_with_metadata(add_args.path, add_args.max_depth,
                                               cat_ptrs, add_args.category_count,
                                               tag_ptrs, add_args.tag_count,
                                               add_args.auto_tag);
                    } else {
                        add_path(add_args.path, add_args.max_depth);
                    }
                } else {
                    printf("Usage: add <path> [-d depth] [-c category...] [-t tag...] [-a]\n");
                }
            }
        }
        else if (strcmp(command, "remove") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: remove <path>\n");
                printf("  Use quotes for paths with spaces: remove \"C:\\Program Files\\App\"\n");
            } else {
                char path[MAX_PATH_LENGTH];
                if (extract_quoted_path(argument, path, sizeof(path), NULL) == 0 && path[0] != '\0') {
                    remove_path_from_db(path);
                } else {
                    printf("Usage: remove <path>\n");
                }
            }
        }
        else if (strcmp(command, "remove-all") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: remove-all <directory>\n");
                printf("  Removes all contents under the specified directory.\n");
                printf("  The directory itself is NOT removed.\n");
                printf("  Use quotes for paths with spaces.\n");
            } else {
                char path[MAX_PATH_LENGTH];
                if (extract_quoted_path(argument, path, sizeof(path), NULL) == 0 && path[0] != '\0') {
                    remove_contents_under_path(path);
                } else {
                    printf("Usage: remove-all <directory>\n");
                }
            }
        }
        else if (strcmp(command, "info") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: info <path>\n");
            } else {
                char path[MAX_PATH_LENGTH];
                if (extract_quoted_path(argument, path, sizeof(path), NULL) == 0 && path[0] != '\0') {
                    show_path_info(path);
                } else {
                    printf("Usage: info <path>\n");
                }
            }
        }
        else if (strcmp(command, "search") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: search <term>\n");
            } else {
                search_paths_all(argument);
            }
        }
        else if (strcmp(command, "search-path") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: search-path <term>\n");
            } else {
                search_fullpath_all(argument);
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
                char path[MAX_PATH_LENGTH];
                if (extract_quoted_path(argument, path, sizeof(path), NULL) == 0 && path[0] != '\0') {
                    list_path_tags(path);
                } else {
                    list_all_tags();
                }
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
        /* ============================================
         * Batch Commands (operate on stored results)
         * ============================================ */
        else if (strcmp(command, "results") == 0) {
            show_stored_results();
        }
        else if (strcmp(command, "clear-results") == 0) {
            clear_stored_results();
        }
        else if (strcmp(command, "tag-results") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: tag-results <tagname>\n");
            } else if (g_last_results.count == 0) {
                printf("No stored results. Run a search first.\n");
            } else {
                int tag_id = get_or_create_tag_with_check(argument);
                if (tag_id >= 0) {
                    int tagged = 0;
                    for (int i = 0; i < g_last_results.count; i++) {
                        int path_id = get_path_id(g_last_results.paths[i]);
                        if (path_id >= 0) {
                            tag_path_by_id(path_id, tag_id);
                            tagged++;
                        }
                    }
                    printf("Tagged %d items with '%s'\n", tagged, argument);
                }
            }
        }
        else if (strcmp(command, "untag-results") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: untag-results <tagname>\n");
            } else if (g_last_results.count == 0) {
                printf("No stored results. Run a search first.\n");
            } else {
                int tag_id = get_tag_id(argument);
                if (tag_id < 0) {
                    printf("Tag not found: %s\n", argument);
                } else {
                    int untagged = 0;
                    for (int i = 0; i < g_last_results.count; i++) {
                        int path_id = get_path_id(g_last_results.paths[i]);
                        if (path_id >= 0) {
                            untag_path_by_id(path_id, tag_id);
                            untagged++;
                        }
                    }
                    printf("Removed tag '%s' from %d items\n", argument, untagged);
                }
            }
        }
        else if (strcmp(command, "categorize-results") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: categorize-results <category>\n");
            } else if (g_last_results.count == 0) {
                printf("No stored results. Run a search first.\n");
            } else {
                int cat_id = get_category_id(argument);
                if (cat_id < 0) {
                    printf("Category not found: %s (use 'create-category' first)\n", argument);
                } else {
                    int categorized = 0;
                    for (int i = 0; i < g_last_results.count; i++) {
                        int path_id = get_path_id(g_last_results.paths[i]);
                        if (path_id >= 0) {
                            categorize_path_by_id(path_id, cat_id);
                            categorized++;
                        }
                    }
                    printf("Categorized %d items as '%s'\n", categorized, argument);
                }
            }
        }
        else if (strcmp(command, "uncategorize-results") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: uncategorize-results <category>\n");
            } else if (g_last_results.count == 0) {
                printf("No stored results. Run a search first.\n");
            } else {
                int cat_id = get_category_id(argument);
                if (cat_id < 0) {
                    printf("Category not found: %s\n", argument);
                } else {
                    int uncategorized = 0;
                    for (int i = 0; i < g_last_results.count; i++) {
                        int path_id = get_path_id(g_last_results.paths[i]);
                        if (path_id >= 0) {
                            uncategorize_path_by_id(path_id, cat_id);
                            uncategorized++;
                        }
                    }
                    printf("Removed category '%s' from %d items\n", argument, uncategorized);
                }
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
                char path[MAX_PATH_LENGTH];
                if (extract_quoted_path(argument, path, sizeof(path), NULL) == 0 && path[0] != '\0') {
                    list_path_categories(path);
                } else {
                    list_all_categories();
                }
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
        /* ============================================
         * Category Root Commands
         * ============================================ */
        else if (strcmp(command, "set-root") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: set-root <category> <path>\n");
            } else {
                char category[MAX_TAG_LENGTH], root_path[MAX_PATH_LENGTH];
                parse_two_args(argument, category, sizeof(category), root_path, sizeof(root_path));
                
                if (strlen(category) == 0 || strlen(root_path) == 0) {
                    printf("Usage: set-root <category> <path>\n");
                } else {
                    add_category_root(category, root_path);
                }
            }
        }
        else if (strcmp(command, "unset-root") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: unset-root <category> <path>\n");
            } else {
                char category[MAX_TAG_LENGTH], root_path[MAX_PATH_LENGTH];
                parse_two_args(argument, category, sizeof(category), root_path, sizeof(root_path));
                
                if (strlen(category) == 0 || strlen(root_path) == 0) {
                    printf("Usage: unset-root <category> <path>\n");
                } else {
                    remove_category_root(category, root_path);
                }
            }
        }
        else if (strcmp(command, "roots") == 0) {
            if (strlen(argument) == 0) {
                list_all_roots();
            } else {
                list_category_roots(argument);
            }
        }
        /* ============================================
         * Check/Sync Commands
         * ============================================ */
        else if (strcmp(command, "check") == 0) {
            if (strlen(argument) == 0) {
                printf("Usage: check <path> [-d depth]\n");
                printf("       check -c <category> [--root <path>]\n");
            } else {
                /* Parse check arguments */
                char category[MAX_TAG_LENGTH] = "";
                char path[MAX_PATH_LENGTH] = "";
                char specific_root[MAX_PATH_LENGTH] = "";
                int max_depth = -1;  /* Unlimited by default */
                
                /* Tokenize arguments */
                char args_copy[MAX_INPUT_LENGTH * 2];
                strncpy(args_copy, argument, sizeof(args_copy) - 1);
                args_copy[sizeof(args_copy) - 1] = '\0';
                
                char *tokens[32];
                int token_count = 0;
                char *token = strtok(args_copy, " ");
                while (token && token_count < 32) {
                    tokens[token_count++] = token;
                    token = strtok(NULL, " ");
                }
                
                /* Parse tokens */
                int i = 0;
                while (i < token_count) {
                    if (strcmp(tokens[i], "-c") == 0 && i + 1 < token_count) {
                        strncpy(category, tokens[i + 1], sizeof(category) - 1);
                        i += 2;
                    } else if (strcmp(tokens[i], "--root") == 0 && i + 1 < token_count) {
                        strncpy(specific_root, tokens[i + 1], sizeof(specific_root) - 1);
                        i += 2;
                    } else if (strcmp(tokens[i], "-d") == 0 && i + 1 < token_count) {
                        max_depth = atoi(tokens[i + 1]);
                        if (max_depth < 0) max_depth = 0;
                        i += 2;
                    } else if (path[0] == '\0') {
                        strncpy(path, tokens[i], sizeof(path) - 1);
                        i++;
                    } else {
                        i++;
                    }
                }
                
                if (category[0] != '\0') {
                    /* Category-based check */
                    check_category(category, specific_root);
                    
                    /* Prompt to add new items if any */
                    if (g_check_new.count > 0) {
                        printf("Add %d new items to '%s'? (y/n): ", g_check_new.count, category);
                        fflush(stdout);
                        char response[16];
                        if (read_utf8_line(response, sizeof(response), stdin)) {
                            trim_whitespace(response);
                            if (response[0] == 'y' || response[0] == 'Y') {
                                /* Get additional options */
                                printf("Options: [-t tag...] [-a]\n> ");
                                fflush(stdout);
                                char opts[MAX_INPUT_LENGTH];
                                if (read_utf8_line(opts, sizeof(opts), stdin)) {
                                    trim_whitespace(opts);
                                    
                                    /* Parse options */
                                    const char *cat_ptrs[1] = { category };
                                    char opt_tags[MAX_TAGS][MAX_TAG_LENGTH];
                                    const char *tag_ptrs[MAX_TAGS];
                                    int tag_count = 0;
                                    int auto_tag = 0;
                                    
                                    char opts_copy[MAX_INPUT_LENGTH];
                                    strncpy(opts_copy, opts, sizeof(opts_copy) - 1);
                                    opts_copy[sizeof(opts_copy) - 1] = '\0';
                                    
                                    char *opt_tok = strtok(opts_copy, " ");
                                    int in_tags = 0;
                                    while (opt_tok) {
                                        if (strcmp(opt_tok, "-t") == 0) {
                                            in_tags = 1;
                                        } else if (strcmp(opt_tok, "-a") == 0 || strcmp(opt_tok, "--auto-tag") == 0) {
                                            auto_tag = 1;
                                            in_tags = 0;
                                        } else if (opt_tok[0] == '-') {
                                            in_tags = 0;
                                        } else if (in_tags && tag_count < MAX_TAGS) {
                                            strncpy(opt_tags[tag_count], opt_tok, MAX_TAG_LENGTH - 1);
                                            opt_tags[tag_count][MAX_TAG_LENGTH - 1] = '\0';
                                            tag_ptrs[tag_count] = opt_tags[tag_count];
                                            tag_count++;
                                        }
                                        opt_tok = strtok(NULL, " ");
                                    }
                                    
                                    add_check_new_items(cat_ptrs, 1, tag_ptrs, tag_count, auto_tag);
                                }
                            }
                        }
                    }
                    
                    /* Prompt to remove missing items if any */
                    if (g_check_missing.count > 0) {
                        if (get_confirmation("Remove missing items from database?")) {
                            remove_check_missing_items();
                        }
                    }
                } else if (path[0] != '\0') {
                    /* Path-based check */
                    check_path(path, max_depth);
                    
                    /* Prompt to add new items if any */
                    if (g_check_new.count > 0) {
                        printf("Add %d new items? (y/n): ", g_check_new.count);
                        fflush(stdout);
                        char response[16];
                        if (read_utf8_line(response, sizeof(response), stdin)) {
                            trim_whitespace(response);
                            if (response[0] == 'y' || response[0] == 'Y') {
                                /* Get additional options */
                                printf("Options: [-c cat...] [-t tag...] [-a]\n> ");
                                fflush(stdout);
                                char opts[MAX_INPUT_LENGTH];
                                if (read_utf8_line(opts, sizeof(opts), stdin)) {
                                    trim_whitespace(opts);
                                    
                                    /* Parse options */
                                    char opt_cats[MAX_CATEGORIES][MAX_TAG_LENGTH];
                                    char opt_tags[MAX_TAGS][MAX_TAG_LENGTH];
                                    const char *cat_ptrs[MAX_CATEGORIES];
                                    const char *tag_ptrs[MAX_TAGS];
                                    int cat_count = 0;
                                    int tag_count = 0;
                                    int auto_tag = 0;
                                    
                                    char opts_copy[MAX_INPUT_LENGTH];
                                    strncpy(opts_copy, opts, sizeof(opts_copy) - 1);
                                    opts_copy[sizeof(opts_copy) - 1] = '\0';
                                    
                                    char *opt_tok = strtok(opts_copy, " ");
                                    int mode = 0;  /* 0=none, 1=cats, 2=tags */
                                    while (opt_tok) {
                                        if (strcmp(opt_tok, "-c") == 0) {
                                            mode = 1;
                                        } else if (strcmp(opt_tok, "-t") == 0) {
                                            mode = 2;
                                        } else if (strcmp(opt_tok, "-a") == 0 || strcmp(opt_tok, "--auto-tag") == 0) {
                                            auto_tag = 1;
                                            mode = 0;
                                        } else if (opt_tok[0] == '-') {
                                            mode = 0;
                                        } else if (mode == 1 && cat_count < MAX_CATEGORIES) {
                                            strncpy(opt_cats[cat_count], opt_tok, MAX_TAG_LENGTH - 1);
                                            opt_cats[cat_count][MAX_TAG_LENGTH - 1] = '\0';
                                            cat_ptrs[cat_count] = opt_cats[cat_count];
                                            cat_count++;
                                        } else if (mode == 2 && tag_count < MAX_TAGS) {
                                            strncpy(opt_tags[tag_count], opt_tok, MAX_TAG_LENGTH - 1);
                                            opt_tags[tag_count][MAX_TAG_LENGTH - 1] = '\0';
                                            tag_ptrs[tag_count] = opt_tags[tag_count];
                                            tag_count++;
                                        }
                                        opt_tok = strtok(NULL, " ");
                                    }
                                    
                                    add_check_new_items(cat_ptrs, cat_count, tag_ptrs, tag_count, auto_tag);
                                }
                            }
                        }
                    }
                    
                    /* Prompt to remove missing items if any */
                    if (g_check_missing.count > 0) {
                        if (get_confirmation("Remove missing items from database?")) {
                            remove_check_missing_items();
                        }
                    }
                } else {
                    printf("Usage: check <path> [-d depth]\n");
                    printf("       check -c <category> [--root <path>]\n");
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
