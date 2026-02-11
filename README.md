## CLI Command Reference

### v4.4 Commands (Current)
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

Potential future improvements for search:
1. Prefix/length filtering
2. First-character filtering
3. Construct a hybrid backend with BK-tree data structure
   (current SQLite implementation of fuzzy search on B-tree backend visits every leaf)
