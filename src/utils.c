/*
 * FileSearch - Utility Functions Implementation
 */

#include "utils.h"

/* ============================================
 * UTF-8 and Locale Support
 * ============================================ */

void init_utf8_support(void) {
    setlocale(LC_ALL, "");
    
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

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
    
    if (!fgets(response, sizeof(response), stdin)) {
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
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return 0;
}

int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
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
