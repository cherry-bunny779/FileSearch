# FileSearch - Feature Changelog

## CLI Command Reference

### v4 Commands (Current)
```
Path Commands:
  add <directory> [-d N]             - Add directory to database
                                       -d 0: directory only (no contents)
                                       -d 1: immediate children only
                                       -d N: recurse N levels deep
                                       (omit -d for unlimited recursion)
  remove <path>                      - Remove path from database
  info <path>                        - Show path details

Search Commands:
  search <term>                      - All search methods
  exact <term>                       - Exact match
  prefix <term>                      - Prefix match
  substring <term>                   - Substring match
  fuzzy <term> [n]                   - Fuzzy match
  find [options]                     - Structured search with filters:
       --category, -c <name>           Filter by category (exact)
       --tag, -t <name>                Filter by tag (exact/substring)
       --tag-fuzzy, -tf <name>         Filter by tag (fuzzy match)
       --name, -n <term>               Filter by path name (substring)
       --fuzzy-distance, -fd <n>       Set fuzzy distance (default: 3)

Tag Commands:
  tag <path> <tagname>               - Add tag to path
  untag <path> <tagname>             - Remove tag from path
  tags [path]                        - List tags
  tagsearch <term>                   - Search tags
  delete-tag <name>                  - Delete a tag completely
  rename-tag <old> <new>             - Rename a tag
  prune-tags                         - Remove all unused tags

Category Commands:
  categorize <path> <category>       - Add category to path
  uncategorize <path> <category>     - Remove category
  categories [path]                  - List categories
  create-category <name>             - Create category
  delete-category <name>             - Delete a category
  rename-category <old> <new>        - Rename a category

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
# Build from src/ directory
make -C src

# Clean build
make -C src clean      # Remove application objects
make -C src cleanall   # Remove all objects including SQLite
```

## Usage

```bash
# Default database location (~/.filesearch/filesearch.db)
./filesearch

# Custom database location
./filesearch --db /path/to/custom.db
```

---

## Version 4: Code Library Reorganization & Issue Fixes

### Code Reorganization
- **Modular architecture**
  - Split monolithic `filesearch_v3.c` into separate source files
  - Header/implementation pairs for each module
  - Clear separation of concerns

- **Module breakdown**
  | File | Purpose |
  |------|---------|
  | `main.c` | Entry point, CLI loop, command parsing |
  | `utils.h/c` | String utilities, path handling, Levenshtein, UTF-8 |
  | `database.h/c` | SQLite initialization, settings, schema, migrations |
  | `paths.h/c` | Path CRUD, directory scanning with depth control |
  | `tags.h/c` | Tag CRUD, delete, rename, prune, similarity checks |
  | `categories.h/c` | Category CRUD, path-category associations |
  | `search.h/c` | Path search, structured search with fuzzy tag support |

### Cross-Platform Makefile
- **OS detection**
  - Automatic detection of Windows, macOS, and Linux
  - Platform-specific linker flags (`-ldl` for Linux only)
  - Platform-specific executable name (`.exe` for Windows)
  
- **Build environments**
  - Native Windows (cmd.exe) with `del` command
  - MSYS2/MinGW with Unix-style `rm`
  - macOS and Linux with standard tools

- **SQLite amalgamation**
  - Compiles `sqlite3.c` from `../deps/` directory
  - Warning suppression for SQLite source (`-Wno-unused-parameter`, `-Wno-unused-but-set-variable`)
  - Separate `cleanall` target for SQLite object

### New Features

#### Depth-Controlled Directory Scanning (Issue #2 Fix)
- **Depth flag for add command**
  - `add <directory> -d 0` - Add directory only, no contents
  - `add <directory> -d 1` - Add immediate children only
  - `add <directory> -d N` - Recurse N levels deep
  - `add <directory>` - Unlimited recursion (default)

- **Use case**
  - Games folder: `add /Games -d 1` to add only game folders, not all internal files

#### Tag Deletion & Management (Issue #3 Fix)
- **Delete tag command**
  - `delete-tag <name>` - Completely remove a tag from database
  - Prompts for confirmation if tag is in use
  - Cascade removes all path-tag associations

- **Rename tag command**
  - `rename-tag <old> <new>` - Rename existing tag
  - Checks for name conflicts before renaming

- **Prune unused tags**
  - `prune-tags` - Remove all tags with zero associations
  - Lists unused tags before deletion
  - Prompts for confirmation

#### Fuzzy Tag Search in Find Command (Issue #4 Fix)
- **Fuzzy tag matching**
  - `find --tag-fuzzy <name>` or `find -tf <name>`
  - Uses Levenshtein distance for approximate matching
  - Reports number of matching tags found

- **Configurable fuzzy distance**
  - `find --fuzzy-distance N` or `find -fd N`
  - Default distance: 3 (from settings)

- **Example usage**
  ```
  find --tag-fuzzy valv           # Matches "valve", "vala", etc.
  find -tf cod -fd 2              # Fuzzy search with distance 2
  find -c Games -tf valv          # Combine with category filter
  ```

#### Category Management
- **Delete category command**
  - `delete-category <name>` - Remove category from database
  - Prompts for confirmation if category is in use

- **Rename category command**
  - `rename-category <old> <new>` - Rename existing category

### Bug Fixes
- **Stats output for tags**
  - Fixed unused variable `tags_in_use` 
  - Now displays: `Tags: X (Y in use)` consistent with categories
  - Shows unused tag hint: `(N unused, run 'prune-tags' to remove)`

### Schema (v4 - unchanged from v3)
```sql
paths (id, path, name, is_directory, size, parent_path)
categories (id, name)
path_categories (path_id, category_id)
tags (id, name)
path_tags (path_id, tag_id)
settings (key, value)
```

### Default Settings
| Key | Default | Description |
|-----|---------|-------------|
| `schema_version` | 2 | Database schema version |
| `app_version` | 2 | Application version |
| `similarity_threshold` | 3 | Tag similarity warning threshold |
| `max_results` | 20 | Maximum search results |
| `fuzzy_default_distance` | 3 | Default Levenshtein distance |
| `default_scan_depth` | -1 | Default scan depth (-1 = unlimited) |

---

## Version 3: filesearch_v2.c (Categories, Tags & Settings) 

### Features
- Database-stored settings with key-value storage
- Schema versioning with migration support
- Category system with many-to-many relationships
- Enhanced tag system with similarity detection
- Structured search with `find` command
- Path information display with `info` command
- Foreign key enforcement with cascade deletes

### Schema (v3)
```sql
paths (id, path, name, is_directory, size, parent_path)
categories (id, name)
path_categories (path_id, category_id)
tags (id, name)
path_tags (path_id, tag_id)
settings (key, value)
```

### Bug Fixes
- `parse_two_args` buffer overflow - fixed incorrect size parameter usage

---

## Version 2: filesearch.c (First Path Management Update)

### Features
- File-based SQLite storage with persistent database
- Cross-platform support (Windows/Unix)
- Directory scanning with recursion limit
- Path search functionality
- Custom database path via `--db` flag

### Schema (v2)
```sql
paths (id, path, name, is_directory, size, parent_path)
tags (id, name)
path_tags (path_id, tag_id)
```

---

## Version 1: tagsearch.c (Initial Implementation)

### Features
- In-memory SQLite database (no persistence)
- Tag loading from text file
- Search: exact, prefix, substring, fuzzy (Levenshtein)
- Interactive CLI

### Schema (v1)
```sql
tags (id, name)
```

---

## Feature Comparison Matrix

| Feature | v1 | v2 | v3 | v4 |
|---------|----|----|----|----|
| Persistent storage | ✗ | ✓ | ✓ | ✓ |
| Path management | ✗ | ✓ | ✓ | ✓ |
| Depth-controlled scanning | ✗ | ✗ | ✗ | ✓ |
| Tag storage | ✓ | ✓ | ✓ | ✓ |
| Tag deletion/rename | ✗ | ✗ | ✗ | ✓ |
| Prune unused tags | ✗ | ✗ | ✗ | ✓ |
| Path-tag association | ✗ | Schema | ✓ | ✓ |
| Categories | ✗ | ✗ | ✓ | ✓ |
| Category deletion/rename | ✗ | ✗ | ✗ | ✓ |
| Tag similarity warning | ✗ | ✗ | ✓ | ✓ |
| Database settings | ✗ | ✗ | ✓ | ✓ |
| Schema versioning | ✗ | ✗ | ✓ | ✓ |
| Structured search | ✗ | ✗ | ✓ | ✓ |
| Fuzzy tag in find | ✗ | ✗ | ✗ | ✓ |
| Cross-platform | ✗ | ✓ | ✓ | ✓ |
| Modular codebase | ✗ | ✗ | ✗ | ✓ |
| Cross-platform Makefile | ✗ | ✗ | ✗ | ✓ |

---

## Known Issues

### Resolved in v4
- ~~Non-recursive add option~~ → Fixed with `-d` depth flag
- ~~Tags cannot be deleted~~ → Fixed with `delete-tag`, `rename-tag`, `prune-tags`
- ~~Find command lacks fuzzy tag search~~ → Fixed with `--tag-fuzzy` flag

### Remaining
1. Non-UTF8 characters may appear as "?" on some Windows configurations
   - Workaround: Use Windows Terminal with UTF-8 font
