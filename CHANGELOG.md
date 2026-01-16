# FileSearch - Feature Changelog

## CLI Command Reference

### v3 Commands (Current)
```
Path Commands:
  add <directory>                    - Add directory recursively
  remove <path>                      - Remove path from database
  info <path>                        - Show path details

Search Commands:
  search <term>                      - All search methods
  exact <term>                       - Exact match
  prefix <term>                      - Prefix match
  substring <term>                   - Substring match
  fuzzy <term> [n]                   - Fuzzy match
  find --category <c> --tag <t> --name <n>
                                     - Structured search

Tag Commands:
  tag <path> <tagname>               - Add tag to path
  untag <path> <tagname>             - Remove tag from path
  tags [path]                        - List tags
  tagsearch <term>                   - Search tags

Category Commands:
  categorize <path> <category>       - Add category to path
  uncategorize <path> <category>     - Remove category
  categories [path]                  - List categories
  create-category <name>             - Create category

Settings Commands:
  set <key> <value>                  - Modify setting
  get <key>                          - View setting
  settings                           - List all settings

Utility Commands:
  stats                              - Database statistics
  help                               - Show help
  quit / exit                        - Exit program
```


---

## Compilation

```bash
# Version 3 (Current)
gcc -o filesearch ./src/filesearch_v3.c ./deps/sqlite3.c
```

## Usage

```bash
# Default database location
./filesearch

# Custom database location
./filesearch --db /path/to/custom.db
```

## Version 3: filesearch_v3.c (Categories, Tags & Settings) 
```

Path Commands:
  add <directory>                    - Add directory recursively
  remove <path>                      - Remove path from database
  info <path>                        - Show path details

Search Commands:
  search <term>                      - All search methods
  exact <term>                       - Exact match
  prefix <term>                      - Prefix match
  substring <term>                   - Substring match
  fuzzy <term> [n]                   - Fuzzy match
  find --category <c> --tag <t> --name <n>
                                     - Structured search

Tag Commands:
  tag <path> <tagname>               - Add tag to path
  untag <path> <tagname>             - Remove tag from path
  tags [path]                        - List tags
  tagsearch <term>                   - Search tags

Category Commands:
  categorize <path> <category>       - Add category to path
  uncategorize <path> <category>     - Remove category
  categories [path]                  - List categories

  create-category <name>             - Create category

Settings Commands:
  set <key> <value>                  - Modify setting
  get <key>                          - View setting
  settings                           - List all settings

Utility Commands:
  stats                              - Database statistics
  help                               - Show help
  quit / exit                        - Exit program
```

---

## Compilation

```bash
# Version 1
gcc -o tagsearch tagsearch.c -lsqlite3

# Version 3 (Current)
gcc -o filesearch filesearch_v2.c -lsqlite3
```

## Usage

```bash
# Default database location
./filesearch

# Custom database location
./filesearch --db /path/to/custom.db
```

### New Features

#### Database-Stored Settings
- **Settings table**
  - Key-value storage for configuration
  - Replaces compile-time macros for runtime values
  
- **Default settings**
  - `schema_version`: 1
  - `app_version`: 1
  - `similarity_threshold`: 3
  - `max_results`: 20
  - `fuzzy_default_distance`: 3

- **Settings management**
  - `get <key>` - View setting value
  - `set <key> <value>` - Modify setting
  - `settings` - List all settings

#### Schema Versioning & Migration
- **Version tracking**
  - Integer-based schema version
  - Separate app version tracking

- **Migration system**
  - Detects outdated databases
  - Prompts user for confirmation before migration
  - Assigns existing paths to "Uncategorized" category

#### Category System
- **Categories table**
  - Pre-populated defaults: Games, Music, Photos, Documents, Uncategorized
  
- **Many-to-many relationship**
  - Paths can belong to multiple categories
  - Junction table: `path_categories`

- **Category commands**
  - `categorize <path> <category>` - Add category to path
  - `uncategorize <path> <category>` - Remove category from path
  - `categories` - List all categories
  - `categories <path>` - List categories for specific path
  - `create-category <name>` - Create new category

#### Enhanced Tag System
- **Tag similarity detection**
  - Levenshtein distance check
  - Substring match check
  - Configurable threshold via settings

- **Tag creation workflow**
  - Warning when similar tag exists
  - Option to use existing tag instead
  - Option to proceed with new tag creation

- **Tag commands**
  - `tag <path> <tagname>` - Add tag to path (with similarity check)
  - `untag <path> <tagname>` - Remove tag from path
  - `tags` - List all tags
  - `tags <path>` - List tags for specific path
  - `tagsearch <term>` - Search tags (exact, substring, fuzzy)

#### Structured Search
- **Find command**
  - `find --category <cat>` - Filter by category
  - `find --tag <tag>` - Filter by tag
  - `find --name <term>` - Filter by name (substring)
  - Combinable: `find --category Games --tag code --name search`

- **Short flags**
  - `-c` for `--category`
  - `-t` for `--tag`
  - `-n` for `--name`

#### Path Information
- **Info command**
  - `info <path>` - Display path details
  - Shows: path, name, type, size, categories, tags

#### Additional Improvements
- **Remove command**
  - `remove <path>` - Delete path from database

- **Foreign key enforcement**
  - `PRAGMA foreign_keys = ON`
  - Cascade deletes for path removal

- **Additional indexes**
  - `idx_path_categories_path`
  - `idx_path_categories_cat`
  - `idx_path_tags_path`
  - `idx_path_tags_tag`
  - `idx_category_name`

### Schema (v3)
```
paths (id, path, name, is_directory, size, parent_path)
categories (id, name)
path_categories (path_id, category_id)
tags (id, name)
path_tags (path_id, tag_id)
settings (key, value)
```

### Bug Fixes
- **parse_two_args buffer overflow**
  - Fixed incorrect size parameter usage
  - Now accepts separate sizes for each argument

### Retained from Previous Versions
- All search algorithms (exact, prefix, substring, fuzzy)
- Levenshtein implementation with SQLite registration
- Cross-platform path handling
- Directory scanning with recursion limit
- Transaction wrapping for bulk operations

---


## Version 2: filesearch.c (First Path Management Update)

### New Features
- **File-based SQLite storage**
  - Persistent database file
  - Default location: `~/.filesearch/filesearch.db`
  - Custom path via `--db` flag

- **Cross-platform support**
  - Windows and Unix path handling
  - Platform-specific home directory detection
  - Conditional compilation for path separators

- **Directory scanning**
  - Recursive directory traversal
  - Stores: path, name, is_directory, size, parent_path
  - Depth limit (100) to prevent infinite recursion
  - Transaction wrapping for bulk inserts

- **Path search functionality**
  - All search modes applied to path names
  - Results display file type and size

- **Directory validation**
  - Check if database directory exists before opening
  - Helpful error messages with platform-specific instructions

### Schema
```
paths (id, path, name, is_directory, size, parent_path)
tags (id, name)
path_tags (path_id, tag_id)
```

### CLI Additions
- `add <directory>` - Recursive directory scanning
- `stats` - Database statistics
- `--db <path>` - Custom database location
- `--help` - Usage information

### Retained from v1
- Tag loading from file (test code)
- All search algorithms
- Levenshtein implementation

---



## Version 1: tagsearch.c (Initial Implementation)

### Core Features
- **In-memory SQLite database**
  - Data stored only during program execution
  - No persistence between sessions

- **Tag storage and search**
  - Load tags from text file
  - Single table schema (`tags`)

- **Search functionality**
  - Exact match (case-insensitive)
  - Prefix match (`LIKE 'term%'`)
  - Substring match (`LIKE '%term%'`)
  - Fuzzy match (Levenshtein distance)

- **Levenshtein distance implementation**
  - Custom C implementation
  - Space-optimized O(min(m,n)) using two-row technique
  - Case-insensitive comparison
  - Registered as SQLite custom function

- **Interactive CLI**
  - Commands: `search`, `exact`, `prefix`, `substring`, `fuzzy`, `list`, `help`, `quit`
  - Configurable max distance for fuzzy search

### Limitations
- No file/folder path management
- No persistent storage
- No categories
- No path-tag associations
- Hardcoded configuration values (macros)

---

## Feature Comparison Matrix

| Feature | v1 (tagsearch) | v2 (filesearch) | v3 (filesearch_v2) |
|---------|----------------|-----------------|---------------------|
| Persistent storage | ✗ | ✓ | ✓ |
| Path management | ✗ | ✓ | ✓ |
| Tag storage | ✓ | ✓ | ✓ |
| Path-tag association | ✗ | Schema only | ✓ |
| Categories | ✗ | ✗ | ✓ |
| Path-category association | ✗ | ✗ | ✓ |
| Tag similarity warning | ✗ | ✗ | ✓ |
| Substring similarity check | ✗ | ✗ | ✓ |
| Database settings | ✗ | ✗ | ✓ |
| Schema versioning | ✗ | ✗ | ✓ |
| Migration prompts | ✗ | ✗ | ✓ |
| Structured search | ✗ | ✗ | ✓ |
| Cross-platform | ✗ | ✓ | ✓ |
| Custom DB path | ✗ | ✓ | ✓ |

---

## Known Issues
```
1. Non-uft8 characters appear as "?"s and cannot be added as paths and tags (v3)

2. Some items do not need recursive add (e.g. Games), as not all of the contents under the directory is important for searching purposes (v3)

3. Tags cannot be removed once created, although they can be unassigned to an item (v3)

4. The "find" command does not incorporate tag search methods, making uncertain searches a multi-step process (v3)