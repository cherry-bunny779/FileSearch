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

Potential future improvements for search:
1. Prefix/length filtering
2. First-character filtering
3. Construct a hybrid backend with BK-tree data structure
   (current SQLite implementation of fuzzy search on B-tree backend visits every leaf)
