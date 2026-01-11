Currently includes an example of fuzzy search by loading a text file, building a SQLite database, 
and giving search results based on the Levenshtein distance between the entries.

Potential future improvements for search:
1. Prefix/length filtering
2. First-character filtering
3. Construct a hybrid backend with BK-tree data structure
   (current SQLite implementation of fuzzy search on B-tree backend visits every leaf)
