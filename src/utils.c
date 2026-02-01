/*
 * FileSearch - Utility Functions Implementation
 */

#include "utils.h"

#ifdef _WIN32
#include <wchar.h>
#endif

/* ============================================
 * UTF-8 and Locale Support
 * ============================================ */

void init_utf8_support(void) {
    setlocale(LC_ALL, "");
    
#ifdef _WIN32
    /* Set console to UTF-8 code page for output */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

#ifdef _WIN32
/*
 * Convert UTF-8 string to Windows wide string (UTF-16).
 * Returns newly allocated wchar_t* that caller must free.
 * Returns NULL on failure.
 */
wchar_t *utf8_to_wide(const char *utf8_str) {
    if (!utf8_str) return NULL;
    
    /* Get required buffer size */
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
    if (wide_len == 0) return NULL;
    
    wchar_t *wide_str = malloc(wide_len * sizeof(wchar_t));
    if (!wide_str) return NULL;
    
    /* Perform conversion */
    if (MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wide_str, wide_len) == 0) {
        free(wide_str);
        return NULL;
    }
    
    return wide_str;
}

/*
 * Convert Windows wide string (UTF-16) to UTF-8.
 * Returns newly allocated char* that caller must free.
 * Returns NULL on failure.
 */
char *wide_to_utf8(const wchar_t *wide_str) {
    if (!wide_str) return NULL;
    
    /* Get required buffer size */
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, NULL, 0, NULL, NULL);
    if (utf8_len == 0) return NULL;
    
    char *utf8_str = malloc(utf8_len);
    if (!utf8_str) return NULL;
    
    /* Perform conversion */
    if (WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, utf8_str, utf8_len, NULL, NULL) == 0) {
        free(utf8_str);
        return NULL;
    }
    
    return utf8_str;
}
#endif

/* ============================================
 * String Utilities
 * ============================================ */

int min3(int a, int b, int c) {
    int min = a;
    if (b < min) min = b;
    if (c < min) min = c;
    return min;
}

void trim_whitespace(char *str) {
    char *start = str;
    while (*start && isspace((unsigned char)*start)) start++;
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

void str_to_lower(char *str) {
    for (char *p = str; *p; p++) {
        *p = tolower((unsigned char)*p);
    }
}

/* ============================================
 * User Interaction
 * ============================================ */

int get_confirmation(const char *prompt) {
    char response[16];
    printf("%s (y/n): ", prompt);
    fflush(stdout);
    
    if (!read_utf8_line(response, sizeof(response), stdin)) {
        return 0;
    }
    
    trim_whitespace(response);
    return (response[0] == 'y' || response[0] == 'Y');
}

/* ============================================
 * Path Utilities
 * ============================================ */

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

int directory_exists(const char *path) {
#ifdef _WIN32
    wchar_t *wide_path = utf8_to_wide(path);
    if (!wide_path) return 0;
    
    struct _stat st;
    int result = 0;
    if (_wstat(wide_path, &st) == 0) {
        result = (st.st_mode & _S_IFDIR) != 0;
    }
    free(wide_path);
    return result;
#else
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return 0;
#endif
}

int file_exists(const char *path) {
#ifdef _WIN32
    wchar_t *wide_path = utf8_to_wide(path);
    if (!wide_path) return 0;
    
    struct _stat st;
    int result = (_wstat(wide_path, &st) == 0);
    free(wide_path);
    return result;
#else
    struct stat st;
    return (stat(path, &st) == 0);
#endif
}

long long get_file_size(const char *path) {
#ifdef _WIN32
    wchar_t *wide_path = utf8_to_wide(path);
    if (!wide_path) return -1;
    
    struct _stat st;
    long long size = -1;
    if (_wstat(wide_path, &st) == 0 && !(st.st_mode & _S_IFDIR)) {
        size = (long long)st.st_size;
    }
    free(wide_path);
    return size;
#else
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        return (long long)st.st_size;
    }
    return -1;
#endif
}

int is_regular_file(const char *path) {
#ifdef _WIN32
    wchar_t *wide_path = utf8_to_wide(path);
    if (!wide_path) return 0;
    
    struct _stat st;
    int result = 0;
    if (_wstat(wide_path, &st) == 0) {
        result = !(st.st_mode & _S_IFDIR);
    }
    free(wide_path);
    return result;
#else
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISREG(st.st_mode);
    }
    return 0;
#endif
}

void get_directory_from_path(const char *filepath, char *dir_buffer, size_t size) {
    strncpy(dir_buffer, filepath, size - 1);
    dir_buffer[size - 1] = '\0';
    
    char *last_sep = strrchr(dir_buffer, PATH_SEPARATOR);
    if (last_sep) {
        *last_sep = '\0';
    }
}

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
 * Argument Parsing
 * ============================================ */

void parse_two_args(const char *input, char *arg1, size_t size1, char *arg2, size_t size2) {
    arg1[0] = '\0';
    arg2[0] = '\0';
    
    /* Handle quoted first argument */
    const char *remainder = NULL;
    if (extract_quoted_path(input, arg1, size1, &remainder) == 0 && remainder) {
        /* Skip whitespace after first arg */
        while (*remainder && isspace(*remainder)) remainder++;
        if (*remainder) {
            strncpy(arg2, remainder, size2 - 1);
            arg2[size2 - 1] = '\0';
            trim_whitespace(arg2);
        }
        return;
    }
    
    /* Fallback to original behavior for backward compatibility */
    const char *last_space = strrchr(input, ' ');
    
    if (last_space) {
        size_t path_len = last_space - input;
        if (path_len >= size1) path_len = size1 - 1;
        strncpy(arg1, input, path_len);
        arg1[path_len] = '\0';
        trim_whitespace(arg1);
        
        strncpy(arg2, last_space + 1, size2 - 1);
        arg2[size2 - 1] = '\0';
        trim_whitespace(arg2);
    }
}

/*
 * Extract a path from input that may be quoted.
 * Supports both double quotes ("path") and single quotes ('path').
 * If not quoted, extracts until the first whitespace.
 * 
 * Returns: 0 on success, -1 on error (unclosed quote)
 * Sets *remainder to point to the character after the path (and closing quote if any)
 */
int extract_quoted_path(const char *input, char *path, size_t path_size, const char **remainder) {
    path[0] = '\0';
    if (remainder) *remainder = NULL;
    
    if (!input || !path || path_size == 0) {
        return -1;
    }
    
    /* Skip leading whitespace */
    const char *p = input;
    while (*p && isspace(*p)) p++;
    
    if (*p == '\0') {
        return -1;  /* Empty input */
    }
    
    char quote_char = 0;
    const char *start;
    const char *end;
    
    /* Check for quoted string */
    if (*p == '"' || *p == '\'') {
        quote_char = *p;
        start = p + 1;  /* Skip opening quote */
        
        /* Find closing quote */
        end = strchr(start, quote_char);
        if (!end) {
            return -1;  /* Unclosed quote */
        }
        
        /* Copy path content */
        size_t len = end - start;
        if (len >= path_size) len = path_size - 1;
        strncpy(path, start, len);
        path[len] = '\0';
        
        if (remainder) {
            *remainder = end + 1;  /* Point past closing quote */
        }
    } else {
        /* Unquoted - extract until whitespace */
        start = p;
        end = start;
        while (*end && !isspace(*end)) end++;
        
        size_t len = end - start;
        if (len >= path_size) len = path_size - 1;
        strncpy(path, start, len);
        path[len] = '\0';
        
        if (remainder) {
            *remainder = end;
        }
    }
    
    return 0;
}

/* ============================================
 * UTF-8 Line Input
 * ============================================ */

#ifdef _WIN32
/*
 * Read a line of UTF-8 text from stdin on Windows.
 * Uses ReadConsoleW for proper Unicode support.
 */
char *read_utf8_line(char *buffer, size_t size, FILE *stream) {
    if (stream != stdin) {
        /* For non-stdin, fall back to regular fgets */
        return fgets(buffer, (int)size, stream);
    }
    
    /* Get console input handle */
    HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        return fgets(buffer, (int)size, stream);  /* Fallback */
    }
    
    /* Check if stdin is a console (not redirected) */
    DWORD mode;
    if (!GetConsoleMode(hConsole, &mode)) {
        /* Not a console, use regular fgets */
        return fgets(buffer, (int)size, stream);
    }
    
    /* Allocate wide character buffer */
    size_t wbuf_size = size;
    wchar_t *wbuffer = malloc(wbuf_size * sizeof(wchar_t));
    if (!wbuffer) {
        return fgets(buffer, (int)size, stream);  /* Fallback */
    }
    
    /* Read wide string from console */
    DWORD chars_read = 0;
    BOOL success = ReadConsoleW(hConsole, wbuffer, (DWORD)(wbuf_size - 1), &chars_read, NULL);
    
    if (!success || chars_read == 0) {
        free(wbuffer);
        return NULL;
    }
    
    /* Null-terminate */
    wbuffer[chars_read] = L'\0';
    
    /* Convert to UTF-8 */
    char *utf8_str = wide_to_utf8(wbuffer);
    free(wbuffer);
    
    if (!utf8_str) {
        buffer[0] = '\0';
        return buffer;
    }
    
    /* Copy to output buffer */
    strncpy(buffer, utf8_str, size - 1);
    buffer[size - 1] = '\0';
    free(utf8_str);
    
    return buffer;
}
#else
/*
 * On Unix/Linux/macOS, just use regular fgets.
 * These systems typically use UTF-8 natively.
 */
char *read_utf8_line(char *buffer, size_t size, FILE *stream) {
    return fgets(buffer, (int)size, stream);
}
#endif

/* ============================================
 * UTF-8 Output
 * ============================================ */

#include <stdarg.h>

#ifdef _WIN32
/*
 * Print a UTF-8 string to the Windows console.
 * Converts to wide string and uses WriteConsoleW.
 */
void print_utf8(const char *str) {
    if (!str) return;
    
    /* Get console output handle */
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        fputs(str, stdout);  /* Fallback */
        return;
    }
    
    /* Check if stdout is a console (not redirected) */
    DWORD mode;
    if (!GetConsoleMode(hConsole, &mode)) {
        /* Not a console, use regular fputs */
        fputs(str, stdout);
        return;
    }
    
    /* Convert UTF-8 to wide string */
    wchar_t *wide_str = utf8_to_wide(str);
    if (!wide_str) {
        fputs(str, stdout);  /* Fallback */
        return;
    }
    
    /* Write to console */
    DWORD written;
    WriteConsoleW(hConsole, wide_str, (DWORD)wcslen(wide_str), &written, NULL);
    free(wide_str);
}

void println_utf8(const char *str) {
    print_utf8(str);
    print_utf8("\n");
}

/*
 * Printf-style function for UTF-8 output on Windows.
 */
int printf_utf8(const char *format, ...) {
    char buffer[4096];
    va_list args;
    
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    print_utf8(buffer);
    return len;
}

#else
/*
 * On Unix/Linux/macOS, just use regular output functions.
 */
void print_utf8(const char *str) {
    if (str) fputs(str, stdout);
}

void println_utf8(const char *str) {
    if (str) puts(str);
    else putchar('\n');
}

int printf_utf8(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int len = vprintf(format, args);
    va_end(args);
    return len;
}
#endif

/* ============================================
 * Levenshtein Distance
 * ============================================ */

int levenshtein(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    /* Ensure s1 is the shorter string for space optimization */
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
            int cost = (tolower((unsigned char)s1[i-1]) == 
                        tolower((unsigned char)s2[j-1])) ? 0 : 1;
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

int is_substring_match(const char *s1, const char *s2) {
    char lower1[MAX_TAG_LENGTH];
    char lower2[MAX_TAG_LENGTH];
    
    strncpy(lower1, s1, sizeof(lower1) - 1);
    lower1[sizeof(lower1) - 1] = '\0';
    strncpy(lower2, s2, sizeof(lower2) - 1);
    lower2[sizeof(lower2) - 1] = '\0';
    
    str_to_lower(lower1);
    str_to_lower(lower2);
    
    return (strstr(lower1, lower2) != NULL || strstr(lower2, lower1) != NULL);
}

/* ============================================
 * Tag Extraction from Filename Patterns
 * ============================================ */

/*
 * Extract tags from a filename with pattern [tag1][tag2]... name
 * Examples:
 *   "[RPG][Bethesda] Skyrim" -> tags: RPG, Bethesda
 *   "[2024][Important] Report.pdf" -> tags: 2024, Important
 *   "Regular filename.txt" -> no tags
 *
 * Returns: number of tags extracted
 */
int extract_tags_from_name(const char *name, ExtractedTags *result) {
    result->count = 0;
    
    if (!name || !result) {
        return 0;
    }
    
    const char *p = name;
    
    while (*p && result->count < MAX_EXTRACTED_TAGS) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        
        /* Look for opening bracket */
        if (*p != '[') {
            break;  /* No more tags */
        }
        
        p++;  /* Skip '[' */
        
        /* Find closing bracket */
        const char *tag_start = p;
        const char *tag_end = strchr(p, ']');
        
        if (!tag_end) {
            break;  /* Malformed - no closing bracket */
        }
        
        /* Extract tag content */
        size_t tag_len = tag_end - tag_start;
        if (tag_len > 0 && tag_len < MAX_TAG_LENGTH) {
            strncpy(result->tags[result->count], tag_start, tag_len);
            result->tags[result->count][tag_len] = '\0';
            
            /* Trim whitespace from tag */
            trim_whitespace(result->tags[result->count]);
            
            /* Only add non-empty tags */
            if (result->tags[result->count][0] != '\0') {
                result->count++;
            }
        }
        
        p = tag_end + 1;  /* Move past ']' */
    }
    
    return result->count;
}

/* ============================================
 * Search Results Storage for Batch Operations
 * ============================================ */

/* Global storage for last search results */
StoredResults g_last_results = { .count = 0 };

void clear_stored_results(void) {
    g_last_results.count = 0;
    printf("Cleared stored results.\n");
}

void store_result(const char *path) {
    if (g_last_results.count >= MAX_STORED_RESULTS) {
        return;  /* Storage full */
    }
    
    strncpy(g_last_results.paths[g_last_results.count], path, MAX_PATH_LENGTH - 1);
    g_last_results.paths[g_last_results.count][MAX_PATH_LENGTH - 1] = '\0';
    g_last_results.count++;
}

void show_stored_results(void) {
    if (g_last_results.count == 0) {
        printf("No stored results. Run a search first.\n");
        return;
    }
    
    printf("\n[Stored Results: %d items]\n", g_last_results.count);
    for (int i = 0; i < g_last_results.count; i++) {
        printf_utf8("  %d. %s\n", i + 1, g_last_results.paths[i]);
    }
    printf("\n");
}
