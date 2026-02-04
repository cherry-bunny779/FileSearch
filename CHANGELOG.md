# FileSearch - Feature Changelog

## CLI Command Reference

### v4.5 Commands (Current)
```
Note: Use quotes for paths with spaces, e.g., "C:\Program Files\App"
      Arrow keys work on macOS/Linux for command history (readline support)

Path Commands:
  add <path> [-d N] [-c cat...] [-t tag...] [-a]
                                     - Add file or directory to database
                                       -d N: recursion depth (0=no contents)
                                       -c: assign categories to ALL items
                                       -t: assign tags to ALL items
                                       -a: auto-tag from [tag] patterns in names
  remove <path>                      - Remove path from database
  remove-all <directory>             - Remove all contents under directory
  info <path>                        - Show path details

Search Commands:
  search <term>                      - Search by filename (all methods)
  search-path <term>                 - Search by full path (all methods)
  exact <term>                       - Exact match on names
  prefix <term>                      - Prefix match on names
  substring <term>                   - Substring match on names
  fuzzy <term> [n]                   - Fuzzy match
  find [options]                     - Structured search with filters:
       --category, -c <name>           Filter by category (exact)
       --tag, -t <name>                Filter by tag (exact/substring)
       --tag-fuzzy, -tf <name>         Filter by tag (fuzzy match)
       --name, -n <term>               Filter by path name (substring)
       --fuzzy-distance, -fd <n>       Set fuzzy distance (default: 3)


Batch Commands (operate on last search results):
  results                            - Show stored search results
  clear-results                      - Clear stored results
  tag-results <tag>                  - Add tag to all stored results
  untag-results <tag>                - Remove tag from all stored results
  categorize-results <cat>           - Add category to all stored results
  uncategorize-results <cat>         - Remove category from all stored results
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
  set-root <category> <path>         - Add root directory for a category
  unset-root <category> <path>       - Remove root directory from a category
  roots [category]                   - List all roots, or roots for a category

Check/Sync Commands:
  check <path> [-d N]                - Check path against database (find new/missing)
  check -c <category>                - Check all roots for a category
  check -c <category> --root <path>  - Check specific root for a category

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

## Project Structure

```
Filesearch/
├── src/
│   ├── Makefile          - Cross-platform build configuration
│   ├── main.c            - Entry point, CLI loop
│   ├── utils.h/c         - String utilities, path handling, Levenshtein, UTF-8
│   ├── database.h/c      - SQLite init, settings, schema, migrations
│   ├── paths.h/c         - Path CRUD, directory scanning with depth control
│   ├── tags.h/c          - Tag CRUD, delete, rename, prune, similarity
│   ├── categories.h/c    - Category CRUD, path-category associations
│   └── search.h/c        - Path search, structured search with fuzzy tags
└── deps/
    ├── sqlite3.c         - SQLite amalgamation (user provided)
    └── sqlite3.h         - SQLite header (user provided)
```

## Compilation

```bash
# Build from src/ directory
cd Filesearch/src
make

# Clean build
make clean      # Remove application objects
make cleanall   # Remove all objects including SQLite
```

## Usage

```bash
# Default database location (~/.filesearch/filesearch.db)
./filesearch

# Custom database location
./filesearch --db /path/to/custom.db
```

---

## Version 4.5: Check/Sync & Category Roots

### New Features

#### Category Root Directories
- **Categories can now have associated root directories**
- New `category_roots` table in database schema
- Multiple roots per category supported

  **New commands:**
  | Command | Description |
  |---------|-------------|
  | `set-root <category> <path>` | Add root directory for a category |
  | `unset-root <category> <path>` | Remove root from a category |
  | `roots [category]` | List all roots, or roots for a category |

  **Example:**
  ```
  > set-root Games G:\Games
  Set root for 'Games': G:\Games

  > set-root Games D:\SteamLibrary
  Set root for 'Games': D:\SteamLibrary

  > roots Games
  [Roots for 'Games']
    D:\SteamLibrary
    G:\Games
  ```

#### Check/Sync Command
- **Compare filesystem against database to find new and missing items**
- Supports both ad-hoc path checking and category-based checking
- Interactive prompts to add new items or remove missing items

  **Syntax:**
  | Command | Description |
  |---------|-------------|
  | `check <path> [-d N]` | Check path against database |
  | `check -c <category>` | Check all roots for a category |
  | `check -c <category> --root <path>` | Check specific root only |

  **Example workflow:**
  ```
  > check -c Games

  Checking category 'Games' (2 roots)
    Root 1: G:\Games
    Root 2: D:\SteamLibrary

  [New on disk - not in database: 3 items]
    [DIR]  G:\Games\NewGame
    [FILE] G:\Games\NewGame\data.pak (1234567 bytes)
    [FILE] G:\Games\OtherGame\patch.dll (45678 bytes)

  [Missing from disk - in database: 2 items]
    [DIR]  G:\Games\DeletedGame

  Add 3 new items to 'Games'? (y/n): y
  Options: [-t tag...] [-a]
  > -t new -a

  Added 3 items.
  Auto-tagged: 1 tag(s) extracted from filenames.

  Remove missing items from database? (y/n): y
  Removed 2 items (+ 15 children).
  ```

### Bug Fixes
- Fixed UTF-8 display in `get_confirmation()` prompts
- Case-insensitive tag match now notifies user: "Using existing tag 'Music' (matched 'music')"

### Schema Changes
- Added `category_roots` table for mapping categories to root directories

---

## Version 4.4: Auto-tagging & Batch Operations

### New Features

#### Auto-tag from Filename Patterns
- **`-a` or `--auto-tag` flag extracts tags from `[tag]` patterns in filenames**
  - Pattern: `[tag1][tag2] actual name`
  - Example filename: `[RPG][Bethesda] Skyrim Special Edition`
  - Extracted tags: `RPG`, `Bethesda`
  
  ```
  > add /Games/Emulation -d 1 -a
  Scanning directory (depth 1): /Games/Emulation
  Added 15 files and 3 directories.
  Auto-tagged: 24 tag(s) extracted from filenames.
  ```

#### Batch Operations on Search Results
- **Search results are now stored for batch operations**
- New commands operate on stored results:

  | Command | Description |
  |---------|-------------|
  | `results` | Show stored search results |
  | `clear-results` | Clear stored results |
  | `tag-results <tag>` | Add tag to all stored results |
  | `untag-results <tag>` | Remove tag from all stored results |
  | `categorize-results <cat>` | Add category to all stored results |
  | `uncategorize-results <cat>` | Remove category from all stored results |

  **Example workflow:**
  ```
  > search Skyrim
  [Exact Match - Paths]
    [DIR]  /Games/Skyrim
  [Prefix Match - Paths]
    [DIR]  /Games/Skyrim
    [DIR]  /Games/Skyrim/Data
  ...
  [5 results stored - use 'results' to view, 'tag-results <tag>' to batch tag]

  > tag-results rpg
  Tagged 5 items with 'rpg'

  > categorize-results Games
  Categorized 5 items as 'Games'
  ```

### Updated Functions

| Module | New/Updated Functions |
|--------|----------------------|
| `utils.h/c` | `extract_tags_from_name()` - Extract tags from `[tag]` patterns |
| `utils.h/c` | `store_result()`, `show_stored_results()`, `clear_stored_results()` |
| `paths.h/c` | `auto_tag_path()` - Apply auto-tagging to a path |
| `paths.h/c` | `add_path_with_metadata()` - Updated with `auto_tag` parameter |
| `tags.h/c` | `untag_path_by_id()` - Silent untag by ID |
| `categories.h/c` | `uncategorize_path_by_id()` - Silent uncategorize by ID |
| `search.c` | `set_store_results()` - Control result storage |

---

## Version 4.3: Recursive Metadata & Readline Support

### New Features

#### Categories/Tags Apply to All Items in Recursive Add
- **`-c` and `-t` flags now apply to ALL items**
  - Previously only applied to the root item at depth 0
  - Now applies to every file and directory added recursively
  - Example: `add G:\Games\Skyrim -d 2 -c Games -t rpg bethesda`
    - The category "Games" and tags "rpg", "bethesda" are applied to Skyrim folder AND all its contents

#### Readline Support (macOS/Linux)
- **Arrow keys now work for command history**
  - Up/Down arrows navigate through previous commands
  - Left/Right arrows for line editing
  - macOS: Uses system libedit (pre-installed), or Homebrew GNU readline if available
  - Linux: Uses GNU readline
  - Can be disabled: `make READLINE=no`
  - Windows uses native console which already has this functionality

### Build Changes

| Platform | Library | Notes |
|----------|---------|-------|
| macOS (default) | libedit | System-provided, uses `<editline/readline.h>` |
| macOS (Homebrew) | GNU readline | Auto-detected at `/usr/local/opt/readline` or `/opt/homebrew/opt/readline` |
| Linux | GNU readline | Uses `<readline/readline.h>` |
| Windows (native) | N/A | Uses native console (readline disabled) |
| Windows (MSYS2) | GNU readline | Optional |

**Build commands:**
```bash
# Standard build (readline auto-detected)
make cleanall && make

# The build will show what readline library is being used:
# Built filesearch for macOS
#   Readline: yes
#   Using: editline (system libedit)

# If you see escape codes (^[[D), rebuild:
make clean && make

# Disable readline if it causes issues:
make READLINE=no
```

**Verify readline is working:**
- At startup, you should see: "(editline enabled)" or "(readline enabled)"
- If you see "(Tip: Rebuild with 'make' to enable arrow key support)", readline was not compiled in

**Install Homebrew readline (optional, for better compatibility):**
```bash
brew install readline
make cleanall && make
```

---

## Version 4.2: Quoted Path Support & Batch Remove

### New Features

#### Quoted Path Support
- **Paths with spaces now supported**
  - Use double or single quotes: `add "C:\Program Files\Game" -d 0`
  - Works with all path commands: `add`, `remove`, `remove-all`, `info`, `tag`, `untag`, `categorize`, `uncategorize`, `tags`, `categories`
  - Example: `tag "C:\My Documents\Report.pdf" important`

#### Batch Remove Command
- **`remove-all <directory>`** - Remove all contents under a directory
  - Only removes contents, NOT the directory itself
  - Shows count of items to be removed
  - Requires confirmation before deletion
  - Example: `remove-all G:\Games` removes all indexed files/folders under G:\Games

### Updated Functions

| Module | New/Updated Functions |
|--------|----------------------|
| `utils.h/c` | `extract_quoted_path()` - Extract path from quoted or unquoted string |
| `paths.h/c` | `remove_contents_under_path()` - Batch remove with confirmation |

---

## Version 4.1: Enhanced Add Command, Uncategorized Automation & Unicode Fixes

### New Features

#### Single File Addition
- **Add individual files directly**
  - `add /path/to/file.txt` - Add a single file to database
  - Automatically extracts filename as "Name"
  - Stores parent directory path
  - `-d` flag is ignored for files

#### Auto-Assign "Uncategorized" Category
- **Automatic categorization on add**
  - All newly added paths are assigned to "Uncategorized"
  - "Uncategorized" category is created if it doesn't exist
  - Provides immediate visibility in category-based searches

#### Smart Category Management  
- **Remove from "Uncategorized" on categorize**
  - When assigning any category other than "Uncategorized", the path is automatically removed from "Uncategorized"
  - Keeps category assignments clean

#### Expanded Add Command
- **Assign categories and tags on add**
  ```
  add /path [-d N] [-c Cat1 Cat2 ...] [-t tag1 tag2 ...]
  ```
  - `-c` assigns multiple categories (categories must exist)
  - `-t` assigns multiple tags (tags created if needed)
  - Example: `add /Games/Skyrim -d 0 -c Games RPG -t bethesda openworld`

#### Search by Name vs Full Path
- **`search <term>`** - Searches the filename only (Name column)
- **`search-path <term>`** - Searches the full path (Path column)
- All search methods (exact, prefix, substring, fuzzy) work on both

### Bug Fixes

#### Windows Unicode Console Output
- **New `printf_utf8()` function**
  - Uses `WriteConsoleW()` on Windows for proper Unicode display
  - Fixes garbled output for CJK characters (Chinese, Japanese, Korean)
  - Paths with international characters now display correctly
  - Example: `G:\Games\文字冒险游戏备档` displays properly

#### Depth Flag Fix
- **Fixed `-d 0` behavior**
  - Changed `current_depth > max_depth` to `current_depth >= max_depth`
  - `-d 0` now correctly adds directory only with no contents
  - `-d 1` adds immediate children only

### Updated Module Functions

| Module | New Functions |
|--------|---------------|
| `utils.h/c` | `get_file_size()`, `is_regular_file()`, `print_utf8()`, `println_utf8()`, `printf_utf8()` |
| `paths.h/c` | `add_file()`, `add_path()`, `add_path_with_metadata()` |
| `categories.h/c` | `categorize_path_by_id()`, `assign_uncategorized()`, `remove_uncategorized()`, `is_categorized()` |
| `tags.h/c` | `tag_path_by_id()` |
| `search.h/c` | `search_fullpath_*()` functions for path-based search |

---

## Version 4: Code Library Reorganization & Issue Fixes

### Code Reorganization
- **Modular architecture**
  - Split monolithic `filesearch_v2.c` into separate source files
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

| Feature | v1 | v2 | v3 | v4 | v4.1 | v4.2 | v4.3 | v4.4 |
|---------|----|----|----|----|------|------|------|------|
| Persistent storage | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Path management | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Single file addition | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ |
| Quoted paths (spaces) | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ |
| Batch remove (remove-all) | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ |
| Depth-controlled scanning | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Add with categories/tags | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ |
| Recursive metadata apply | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ |
| Auto-tag from [tag] pattern | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| Batch operations (results) | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ |
| Auto "Uncategorized" | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ |
| Tag storage | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Tag deletion/rename | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Prune unused tags | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Path-tag association | ✗ | Schema | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Categories | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Category deletion/rename | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Tag similarity warning | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Database settings | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Schema versioning | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Structured search | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Fuzzy tag in find | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Search by name vs path | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ |
| Cross-platform | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Unicode console output | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ |
| Readline (arrow keys) | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ |
| Modular codebase | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Cross-platform Makefile | ✗ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |

---

## Known Issues

### Resolved in v4.4
- ~~No way to batch tag search results~~ → Fixed with batch commands (tag-results, etc.)
- ~~Manual tagging for files with [tag] pattern names~~ → Fixed with auto-tag (-a flag)

### Resolved in v4.3
- ~~Categories/tags only apply to root item~~ → Fixed: Now applies to all items recursively
- ~~Arrow keys print escape codes on macOS~~ → Fixed with editline/readline support

### macOS Arrow Keys Troubleshooting
If you still see `^[[D` when pressing arrow keys:
1. **Check startup message** - Should show "(editline enabled)" or "(readline enabled)"
2. **Rebuild the application:**
   ```bash
   cd Filesearch/src
   make cleanall && make
   ```
3. **Verify build output** shows "Readline: yes"
4. **If using Homebrew readline**, ensure it's properly linked:
   ```bash
   brew install readline
   make cleanall && make
   ```
5. **As a workaround**, install rlwrap: `brew install rlwrap && rlwrap ./filesearch`

### Resolved in v4.2
- ~~Paths with spaces cannot be added~~ → Fixed with quoted path support
- ~~No batch remove for directory contents~~ → Fixed with `remove-all` command

### Resolved in v4.1
- ~~Cannot add single files~~ → Fixed with `add_file()` function
- ~~No auto-categorization~~ → Fixed with "Uncategorized" auto-assignment
- ~~Unicode paths display garbled~~ → Fixed with `printf_utf8()` using `WriteConsoleW()`
- ~~-d 0 still scans one level~~ → Fixed depth comparison (`>=` instead of `>`)

### Resolved in v4
- ~~Non-recursive add option~~ → Fixed with `-d` depth flag
- ~~Tags cannot be deleted~~ → Fixed with `delete-tag`, `rename-tag`, `prune-tags`
- ~~Find command lacks fuzzy tag search~~ → Fixed with `--tag-fuzzy` flag

### Remaining
1. None currently known
