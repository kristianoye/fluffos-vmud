#ifndef FILE_H
#define FILE_H
/*
 * file.c
 */

#include <string>
#include <sys/types.h>  // for off_t, time_t
#include <vector>

const char* check_valid_path(const char*, object_t*, const char* const, int);
void dump_file_descriptors(outbuffer_t*);

char* read_file(const char*, int, int);
char* read_bytes(const char*, int, int, int*);
int write_file(const char*, const char*, int);
int write_bytes(const char*, int, const char*, int);
array_t* get_dir(const char*, int);
int tail(char*);
int file_size(const char*);
int copy_file(const char*, const char*);
int do_rename(const char*, const char*, int);
int remove_file(const char*);

// A single resolved directory entry: pure stat()/readdir() data, no LPC
// allocation -- safe to build off the main thread (see async_getdir()).
struct DirScanEntry {
  std::string name;
  bool is_dir = false;
  off_t size = 0;
  time_t mtime = 0;
};

// get_dir()'s own wildcard/single-file resolution, factored out so
// async_getdir()'s worker thread can share the exact same semantics.
// Pure stat()/opendir() logic -- no LPC allocation, no master apply -- so
// this is safe to call from any thread, unlike get_dir() itself (which
// calls check_valid_path(), an LPC master apply). Callers must already
// have run `path` through check_valid_path() themselves.
struct DirQuery {
  bool is_single_entry = false;
  DirScanEntry single_entry;  // valid iff is_single_entry
  std::string scan_dir;       // valid iff !is_single_entry: directory to opendir()
  bool has_pattern = false;   // valid iff !is_single_entry
  std::string pattern;        // valid iff has_pattern: wildcard to match entries against
};
bool resolve_dir_query(const char* path, DirQuery* out);

// Lists `query`'s matches (opendir/readdir, stat() per entry iff
// want_stat) into *out, capped at max_entries, sorted by name. Returns
// false (leaving *out untouched) only if the directory couldn't be
// opened at all -- an empty *out on true success means a genuinely empty
// directory. Thread-safe.
bool scan_dir_query(const DirQuery& query, bool want_stat, int max_entries,
                    std::vector<DirScanEntry>* out);


int match_string(const char* match, const char* str);

#ifdef DEBUGMALLOC_EXTENSIONS
void mark_file_sv(void);
#endif

#endif
