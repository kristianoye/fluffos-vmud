/*
 * file: file.c
 * description: handle all file based efuns
 */
#include "base/package_api.h"

#include "base/internal/tracing.h"
#include "packages/core/file.h"

#include <algorithm>
#include <iostream>
#include <cerrno>
#if HAVE_DIRENT_H
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif
#include <fcntl.h>
#include <sstream>
#include <unistd.h>
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#include "base/internal/strutils.h"
#include "ghc/filesystem.hpp"
namespace fs = ghc::filesystem;

/*
 * Credits for some of the code below goes to Free Software Foundation
 * Copyright (C) 1990 Free Software Foundation, Inc.
 * See the GNU General Public License for more details.
 */
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#ifndef S_ISCHR
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#endif

#ifndef S_ISBLK
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#endif

#ifdef _WIN32
#define lstat(x, y) stat(x, y)
#define link(x, y) ((-1))
#define OS_mkdir(x, y) mkdir(x)
#else
#define OS_mkdir(x, y) mkdir(x, y)
#endif

static int do_move(const char* from, const char* to, int flag);
static void encode_stat(svalue_t* /*vp*/, int /*flags*/, const DirScanEntry& /*entry*/);

enum { MAX_LINES = 50 };

static void encode_stat(svalue_t* vp, int flags, const DirScanEntry& entry) {
  if (flags == -1) {
    array_t* v = allocate_empty_array(3);

    v->item[0].type = T_STRING;
    v->item[0].subtype = STRING_MALLOC;
    v->item[0].u.string = string_copy(entry.name.c_str(), "encode_stat");
    v->item[1].type = T_NUMBER;
    v->item[1].u.number = entry.is_dir ? -2 : entry.size;
    v->item[2].type = T_NUMBER;
    v->item[2].u.number = entry.mtime;
    vp->type = T_ARRAY;
    vp->u.arr = v;
  } else {
    vp->type = T_STRING;
    vp->subtype = STRING_MALLOC;
    vp->u.string = string_copy(entry.name.c_str(), "encode_stat");
  }
}

/* WIN32 should be fixed to do this correctly (i.e. no ifdefs for it) */
enum { MAX_FNAME_SIZE = 255, MAX_PATH_LEN = 1024 };

// Case-insensitive-agnostic shell-style wildcard match ('*'/'?'/'\\'
// escape); shared by get_dir() and async_getdir()'s worker thread. Pure
// pointer-walking, no LPC calls -- safe on any thread.
int match_string(const char* match, const char* str) {
  int i;

again:
  if (*str == '\0' && *match == '\0') {
    return 1;
  }
  switch (*match) {
    case '?':
      if (*str == '\0') {
        return 0;
      }
      str++;
      match++;
      goto again;
    case '*':
      match++;
      if (*match == '\0') {
        return 1;
      }
      for (i = 0; str[i] != '\0'; i++) {
        if (match_string(match, str + i)) {
          return 1;
        }
      }
      return 0;
    case '\0':
      return 0;
    case '\\':
      match++;
      if (*match == '\0') {
        return 0;
      }
    /* Fall through ! */
    default:
      if (*match == *str) {
        match++;
        str++;
        goto again;
      }
      return 0;
  }
}

// get_dir()'s wildcard/single-file resolution, extracted so
// async_getdir()'s worker thread can share it -- pure stat()/string
// logic, no LPC allocation, no master apply. `path` must already have
// been through check_valid_path().
bool resolve_dir_query(const char* path, DirQuery* out) {
  struct stat st;
  char temppath[MAX_FNAME_SIZE + MAX_PATH_LEN + 2];
  char regexppath[MAX_FNAME_SIZE + MAX_PATH_LEN + 2];
  char* p;
  int do_match = 0;

  if (!path) {
    return false;
  }

  if (strlen(path) < 2) {
    temppath[0] = path[0] ? path[0] : '.';
    temppath[1] = '\000';
    p = temppath;
  } else {
    strncpy(temppath, path, MAX_FNAME_SIZE + MAX_PATH_LEN + 1);
    temppath[MAX_FNAME_SIZE + MAX_PATH_LEN + 1] = '\0';

    /*
     * If path ends with '/' or "/." remove it
     */
    if ((p = strrchr(temppath, '/')) == nullptr) {
      p = temppath;
    }
    if (p[0] == '/' && ((p[1] == '.' && p[2] == '\0') || p[1] == '\0')) {
      *p = '\0';
    }
  }

  if (stat(temppath, &st) < 0) {
    if (*p == '\0') {
      return false;
    }
    if (p != temppath) {
      strcpy(regexppath, p + 1);
      *p = '\0';
    } else {
      strcpy(regexppath, p);
      strcpy(temppath, ".");
    }
    do_match = 1;
  } else if (*p != '\0' && strcmp(temppath, ".") != 0) {
    if (*p == '/' && *(p + 1) != '\0') {
      p++;
    }
    out->is_single_entry = true;
    out->single_entry.name = p;
    out->single_entry.is_dir = S_ISDIR(st.st_mode);
    out->single_entry.size = st.st_size;
    out->single_entry.mtime = st.st_mtime;
    return true;
  }

  out->is_single_entry = false;
  out->scan_dir = temppath;
  out->has_pattern = do_match != 0;
  if (do_match) {
    out->pattern = regexppath;
  }
  return true;
}

bool scan_dir_query(const DirQuery& query, bool want_stat, int max_entries,
                    std::vector<DirScanEntry>* out) {
  if (query.is_single_entry) {
    out->push_back(query.single_entry);
    return true;
  }

  DIR* dirp = opendir(query.scan_dir.c_str());
  if (dirp == nullptr) {
    return false;
  }

  char pathbuf[MAX_FNAME_SIZE + MAX_PATH_LEN + 2];
  size_t const dirlen = query.scan_dir.size();
  bool const dir_fits = dirlen + 2 <= sizeof(pathbuf);
  if (dir_fits) {
    memcpy(pathbuf, query.scan_dir.c_str(), dirlen);
    pathbuf[dirlen] = '/';
    pathbuf[dirlen + 1] = '\0';
  }

  for (struct dirent* de = readdir(dirp);
       de != nullptr && static_cast<int>(out->size()) < max_entries; de = readdir(dirp)) {
    if (!query.has_pattern && (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)) {
      continue;
    }
    if (query.has_pattern && !match_string(query.pattern.c_str(), de->d_name)) {
      continue;
    }

    DirScanEntry entry;
    entry.name = de->d_name;
    if (want_stat) {
      size_t const namelen = strlen(de->d_name);
      if (dir_fits && dirlen + 1 + namelen < sizeof(pathbuf)) {
        memcpy(pathbuf + dirlen + 1, de->d_name, namelen + 1);
        struct stat st;
        if (stat(pathbuf, &st) == 0) {
          entry.is_dir = S_ISDIR(st.st_mode);
          entry.size = st.st_size;
          entry.mtime = st.st_mtime;
        }
      }
      // else: combined path too long to form; can't stat it, leave zeroed
      // (matches get_dir()'s own "can't form the path" fallback).
    }
    out->push_back(std::move(entry));
  }
  closedir(dirp);

  std::sort(out->begin(), out->end(),
            [](const DirScanEntry& a, const DirScanEntry& b) { return a.name < b.name; });
  return true;
}

/*
 * List files in directory. This function do same as standard list_files did,
 * but instead writing files right away to user this returns an array
 * containing those files.
 * Differences with list_files:
 *
 *   - file_list("/w"); returns ({ "w" })
 *
 *   - file_list("/w/"); and file_list("/w/."); return contents of directory
 *     "/w"
 *
 *   - file_list("/");, file_list("."); and file_list("/."); return contents
 *     of directory "/"
 *
 * With second argument equal to non-zero, instead of returning an array
 * of strings, the function will return an array of arrays about files.
 * The information in each array is supplied in the order:
 *    name of file,
 *    size of file,
 *    last update of file.
 */
array_t* get_dir(const char* path, int flags) {
  auto max_array_size = CONFIG_INT(__MAX_ARRAY_SIZE__);

  if (!path) {
    return nullptr;
  }

  path = check_valid_path(path, current_object, "stat", 0);
  if (path == nullptr) {
    return nullptr;
  }

  DirQuery query;
  if (!resolve_dir_query(path, &query)) {
    return nullptr;
  }

  std::vector<DirScanEntry> entries;
  if (!scan_dir_query(query, flags == -1, max_array_size, &entries)) {
    return nullptr;
  }

  array_t* v = allocate_empty_array(entries.size());
  for (size_t i = 0; i < entries.size(); i++) {
    encode_stat(&v->item[i], flags, entries[i]);
  }
  // scan_dir_query() already returned `entries` sorted by name, and this
  // loop preserves that order 1:1 -- no separate qsort needed here.
  return v;
}

int remove_file(const char* path) {
  path = check_valid_path(path, current_object, "remove_file", 1);

  if (path == nullptr) {
    return 0;
  }
  if (unlink(path) == -1) {
    return 0;
  }
  return 1;
}

/*
 * Append string to file. Return 0 for failure, otherwise 1.
 */
int write_file(const char* file, const char* str, int flags) {
  FILE* f;
#ifdef HAVE_ZLIB
  gzFile gf;
#else
  if (flags & 2) {
    error("write_file: compressed writes are not available on this driver build.\n");
  }
#endif

  file = check_valid_path(file, current_object, "write_file", 1);
  if (!file) {
    return 0;
  }
#ifdef HAVE_ZLIB
  if (flags & 2) {
    gf = gzopen(file, (flags & 1) ? "wb" : "ab");
    if (!gf) {
      error("Wrong permissions for opening file /%s for %s.\n\"%s\"\n", file,
            (flags & 1) ? "overwrite" : "append", strerror(errno));
    }
    gzwrite(gf, str, strlen(str));
    gzclose(gf);
    return 1;
  }
#endif
  f = fopen(file, (flags & 1) ? "wb" : "ab");
  if (f == nullptr) {
    error("Wrong permissions for opening file /%s for %s.\n\"%s\"\n", file,
          (flags & 1) ? "overwrite" : "append", strerror(errno));
  }
  fwrite(str, strlen(str), 1, f);
  fclose(f);
  return 1;
}

/* Reads file, starting from line of "start", with maximum lines of "lines".
 * Returns a malloced_string.
 */
char* read_file(const char* file, int start, int lines) {
  const auto read_file_max_size = CONFIG_INT(__MAX_READ_FILE_SIZE__);

  if (lines < 0) {
    debug(file, "read_file: trying to read negative lines: %d", lines);
    return nullptr;
  }

  const char* real_file;

  real_file = check_valid_path(file, current_object, "read_file", 0);
  if (!real_file) {
    return nullptr;
  }

  try {
    auto fs_real_file = fs::u8path(real_file);

    /*
     * file doesn't exist, or is really a directory
     */
    if (!fs::exists(fs_real_file) || fs::is_directory(fs_real_file)) {
      return nullptr;
    }

    if (fs::is_empty(fs_real_file)) {
      /* zero length file */
      char* result = new_string(0, "read_file: empty");
      result[0] = '\0';
      return result;
    }

  } catch (fs::filesystem_error& err) {
    debug(file, "read_file: filesystem error: %s (%d).\n", err.what(), err.code().value());
    return nullptr;
  }

  // With zlib, reads go through gzopen, which transparently decompresses
  // .gz content and passes plain files through; without it, plain stdio.
#ifdef HAVE_ZLIB
  gzFile f = gzopen(real_file, "rb");
#else
  FILE* f = fopen(real_file, "rb");
#endif

  if (f == nullptr) {
    debug(file, "read_file: fail to open: %s.\n", file);
    return nullptr;
  }

  static char* the_buff = nullptr;
  if (!the_buff) {
    the_buff = reinterpret_cast<char*>(
        DMALLOC(2 * read_file_max_size + 1, TAG_PERMANENT, "read_file: theBuff"));
  }

#ifdef HAVE_ZLIB
  int const total_bytes_read = gzread(f, (void*)the_buff, 2 * read_file_max_size);
  gzclose(f);
#else
  int const total_bytes_read = fread(the_buff, 1, 2 * read_file_max_size, f);
  fclose(f);
#endif

  if (total_bytes_read <= 0) {
    debug(file, "read_file: read error: %s.\n", file);
    return nullptr;
  }
  the_buff[total_bytes_read] = '\0';
  const char* ptr_start = the_buff;

  if (start > 1) {
    // skip forward until the "start"-th line
    while (start > 1 && ptr_start < the_buff + total_bytes_read) {
      if (*ptr_start == '\0') {
        debug(file, "read_file: file contains '\\0': %s.\n", file);
        return nullptr;
      }
      if (*ptr_start == '\n') {
        start--;
      }
      ptr_start++;
    }

    // not found
    if (start > 1) {
      debug(file, "read_file: reached EOF searching for start: %s.\n", file);
      return nullptr;
    }
  } else if (start < 0) {
    // move backwards from end by "start"-th lines
    ptr_start += total_bytes_read - 1;

    // account for non-POSIX line endings at end of file, if not POSIX then
    // move pointer forward so decrementing doesn't clip the last character
    if (*ptr_start != '\n') ptr_start++;

    while (start < 0 && ptr_start > the_buff) {
      ptr_start--;
      if (*ptr_start == '\0') {
        debug(file, "read_file: file contains '\\0': %s.\n", file);
        return nullptr;
      }
      if (*ptr_start == '\n') {
        start++;
      }
      // move pointer past '\n' if we have enough lines
      if (!start) {
        ptr_start++;
      }
    }

    if (start < 0) {
      ptr_start = the_buff;
    }
  }

  char* ptr_end = (char*)the_buff + total_bytes_read;

  if (lines > 0) {
    // continue searching forward for "lines" of '\n'
    ptr_end = (char*)ptr_start;
    while (lines > 0 && ptr_end <= the_buff + total_bytes_read) {
      if (*ptr_end++ == '\n') {
        lines--;
      }
    }
  }

  // Truncate result to read_file_max_size
  if (ptr_end > ptr_start + read_file_max_size) {
    ptr_end = (char*)ptr_start + read_file_max_size;
  }

  // The forward line search can post-increment ptr_end one past the last byte
  // read (to the_buff + total_bytes_read + 1) when it runs off the end without
  // finding enough newlines. Clamp to the terminator slot so the '\0' below
  // stays inside the 2*max+1 byte buffer instead of writing one past it.
  if (ptr_end > the_buff + total_bytes_read) {
    ptr_end = the_buff + total_bytes_read;  // the_buff is already char*
  }

  *ptr_end = '\0';

  bool const found_crlf = strchr(ptr_start, '\r') != nullptr;
  if (found_crlf) {
    // Deal with CRLF.
    std::string content(ptr_start);
    ReplaceStringInPlace(content, "\r\n", "\n");
    return string_copy(content.c_str(), "read file: CRLF result");
  }
  return string_copy(ptr_start, "read_file: result");
}

char* read_bytes(const char* file, int start, int len, int* rlen) {
  const auto max_byte_transfer = CONFIG_INT(__MAX_BYTE_TRANSFER__);

  struct stat st;
  FILE* fptr;
  char* str;
  int size;

  if (len < 0) {
    return nullptr;
  }
  file = check_valid_path(file, current_object, "read_bytes", 0);
  if (!file) {
    return nullptr;
  }
  fptr = fopen(file, "rb");
  if (fptr == nullptr) {
    return nullptr;
  }
  if (fstat(fileno(fptr), &st) == -1) {
    fatal("Could not stat an open file.\n");
  }
  size = st.st_size;
  if (start < 0) {
    start = size + start;
  }

  if (len == 0) {
    len = size;
  }
  if (len > max_byte_transfer) {
    fclose(fptr);
    error("Transfer exceeded maximum allowed number of bytes.\n");
    return nullptr;
  }
  if (start >= size) {
    fclose(fptr);
    return nullptr;
  }
  if ((start + len) > size) {
    len = (size - start);
  }

  if ((size = fseek(fptr, start, 0)) < 0) {
    fclose(fptr);
    return nullptr;
  }

  str = new_string(len, "read_bytes: str");

  size = fread(str, 1, len, fptr);

  fclose(fptr);

  if (size <= 0) {
    FREE_MSTR(str);
    return nullptr;
  }
  /*
   * The string has to end to '\0'!!!
   */
  str[size] = '\0';

  *rlen = size;
  return str;
}

int write_bytes(const char* file, int start, const char* str, int theLength) {
  const auto max_byte_transfer = CONFIG_INT(__MAX_BYTE_TRANSFER__);

  struct stat st;
  int size;
  FILE* fptr;

  file = check_valid_path(file, current_object, "write_bytes", 1);

  if (!file) {
    return 0;
  }
  if (theLength > max_byte_transfer) {
    return 0;
  }
  /* Under system V, it isn't possible change existing data in a file
   * opened for append, so it can't be opened for append.
   * opening for r+ won't create the file if it doesn't exist.
   * opening for w or w+ will truncate it if it does exist.  So we
   * have to check if it exists first.
   */
  if (stat(file, &st) == -1) {
    fptr = fopen(file, "wb");
  } else {
    fptr = fopen(file, "r+b");
  }
  if (fptr == nullptr) {
    return 0;
  }
  if (fstat(fileno(fptr), &st) == -1) {
    fatal("Could not stat an open file.\n");
  }
  size = st.st_size;
  if (start < 0) {
    start = size + start;
  }
  if (start < 0 || start > size) {
    fclose(fptr);
    return 0;
  }
  if ((size = fseek(fptr, start, 0)) < 0) {
    fclose(fptr);
    return 0;
  }
  size = fwrite(str, 1, theLength, fptr);

  fclose(fptr);

  if (size <= 0) {
    return 0;
  }
  return 1;
}

int file_size(const char* file) {
  struct stat st;
  long ret;

  file = check_valid_path(file, current_object, "file_size", 0);
  if (!file) {
    return -1;
  }

  if (stat(file, &st) == -1) {
    ret = -1;
  } else if (S_IFDIR & st.st_mode) {
    ret = -2;
  } else {
    ret = st.st_size;
  }

  return ret;
}

/*
 * Check that a path to a file is valid for read or write.
 * This is done by functions in the master object.
 * The path is always treated as an absolute path, and is returned without
 * a leading '/'.
 * If the path was '/', then '.' is returned.
 * Otherwise, the returned path is temporarily allocated by apply(), which
 * means it will be deallocated at next apply().
 */
const char* check_valid_path(const char* path, object_t* call_object, const char* const call_fun,
                             int writeflg) {
  svalue_t* v;

  if (!master_ob && !call_object) {
    // early startup, ignore security
    free_svalue(&apply_ret_value, "check_valid_path");
    apply_ret_value.type = T_STRING;
    apply_ret_value.subtype = STRING_MALLOC;
    path = apply_ret_value.u.string = string_copy(path, "check_valid_path");
    return path;
  }

  if (call_object == nullptr || call_object->flags & O_DESTRUCTED) {
    return nullptr;
  }

  copy_and_push_string(path);
  push_object(call_object);
  push_constant_string(call_fun);
  if (writeflg) {
    v = safe_apply_master_ob(APPLY_VALID_WRITE, 3);
  } else {
    v = safe_apply_master_ob(APPLY_VALID_READ, 3);
  }

  if (v == (svalue_t*)-1) {
    v = nullptr;
  }

  if (v && v->type == T_NUMBER && v->u.number == 0) {
    return nullptr;
  }
  /* An async valid_read()/valid_write() hands back a promise the instant its
   * body parks -- before it has decided anything. Everything below treats a
   * non-string, non-zero return as "allow", so without this a promise is a
   * silent grant. Deny instead: the driver is the caller here and has nowhere
   * to await, so there is no answer yet, and no answer must not mean yes.
   *
   * define_new_function() refuses `async` on these names outright, but that
   * check keys on the DECLARATION and so cannot see an ordinary valid_read()
   * that returns the result of an async call. This is the backstop that
   * actually closes the hole. */
  if (v && v->type == T_PROMISE) {
    debug_message(
        "%s: %s returned a promise -- an apply cannot be async, and a pending answer is not a "
        "grant. Denying.\n",
        call_fun, writeflg ? "valid_write" : "valid_read");
    return nullptr;
  }
  if (v && v->type == T_STRING) {
    path = v->u.string;
  } else {
    extern svalue_t apply_ret_value;

    free_svalue(&apply_ret_value, "check_valid_path");
    apply_ret_value.type = T_STRING;
    apply_ret_value.subtype = STRING_MALLOC;
    path = apply_ret_value.u.string = string_copy(path, "check_valid_path");
  }

  if (path[0] == '/') {
    path++;
  }
  if (path[0] == '\0') {
    path = ".";
  }
  if (legal_path(path)) {
    return path;
  }

  return nullptr;
}

static int match_string(char* match, char* str) {
  int i;

again:
  if (*str == '\0' && *match == '\0') {
    return 1;
  }
  switch (*match) {
    case '?':
      if (*str == '\0') {
        return 0;
      }
      str++;
      match++;
      goto again;
    case '*':
      match++;
      if (*match == '\0') {
        return 1;
      }
      for (i = 0; str[i] != '\0'; i++) {
        if (match_string(match, str + i)) {
          return 1;
        }
      }
      return 0;
    case '\0':
      return 0;
    case '\\':
      match++;
      if (*match == '\0') {
        return 0;
      }
    /* Fall through ! */
    default:
      if (*match == *str) {
        match++;
        str++;
        goto again;
      }
      return 0;
  }
}

static struct stat to_stats, from_stats;

/* Move FROM onto TO.  Handles cross-filesystem moves.
   If TO is a directory, FROM must be also.
   Return 0 if successful, 1 if an error occurred.  */

#ifdef F_RENAME
static int do_move(const char* from, const char* to, int flag) {
  if (lstat(from, &from_stats) != 0) {
    error("/%s: lstat failed\n", from);
    return 1;
  }
  if (lstat(to, &to_stats) == 0) {
#ifdef __WIN32
    if (strcmp(from, to) == 0) {
#else
    if (from_stats.st_dev == to_stats.st_dev && from_stats.st_ino == to_stats.st_ino) {
#endif
      error("`/%s' and `/%s' are the same file", from, to);
      return 1;
    }
    if (S_ISDIR(to_stats.st_mode)) {
      error("/%s: cannot overwrite directory", to);
      return 1;
    }
  } else if (errno != ENOENT) {
    error("/%s: unknown error\n", to);
    return 1;
  }
  if (flag == F_RENAME) {
    std::error_code error_code;
    fs::rename(from, to, error_code);
    if (!error_code) {
      return 0;
    }
  }
#ifdef F_LINK
  else if (flag == F_LINK) {
    if (link(from, to) == 0) {
      return 0;
    }
  }
#endif

  if (errno != EXDEV) {
    if (flag == F_RENAME) {
      error("cannot move `/%s' to `/%s'\n", from, to);
    } else {
      error("cannot link `/%s' to `/%s'\n", from, to);
    }
    return 1;
  }
  /* rename failed on cross-filesystem link.  Copy the file instead. */
  if (flag == F_RENAME) {
    if (copy_file(from, to)) {
      return 1;
    }
    if (unlink(from)) {
      error("cannot remove `/%s'", from);
      return 1;
    }
  }
#ifdef F_LINK
  else if (flag == F_LINK) {
    if (symlink(from, to) == 0) { /* symbolic link */
      return 0;
    }
  }
#endif
  return 0;
}
#endif

void debug_perror(const char* what, const char* file) {
  if (file) {
    debug_message("System Error: %s:%s:%s\n", what, file, strerror(errno));
  } else {
    debug_message("System Error: %s:%s\n", what, strerror(errno));
  }
}

/*
 * do_rename is used by the efun rename. It is basically a combination
 * of the unix system call rename and the unix command mv.
 */

static svalue_t from_sv = {T_NUMBER};
static svalue_t to_sv = {T_NUMBER};

#ifdef DEBUGMALLOC_EXTENSIONS
void mark_file_sv() {
  mark_svalue(&from_sv);
  mark_svalue(&to_sv);
}
#endif

#ifdef F_RENAME
int do_rename(const char* fr, const char* t, int flag) {
  const char* from;
  const char* to;
  char newfrom[MAX_FNAME_SIZE + MAX_PATH_LEN + 2];
  int flen;
  extern svalue_t apply_ret_value;

  /*
   * important that the same write access checks are done for link() as are
   * done for rename().  Otherwise all kinds of security problems would
   * arise (e.g. creating links to files in protected directories and then
   * modifying the protected file by modifying the linked file). The idea
   * is prevent linking to a file unless the person doing the linking has
   * permission to move the file.
   */
  from = check_valid_path(fr, current_object, "rename", 1);
  if (!from) {
    return 1;
  }

  assign_svalue(&from_sv, &apply_ret_value);

  to = check_valid_path(t, current_object, "rename", 1);
  if (!to) {
    return 1;
  }

  assign_svalue(&to_sv, &apply_ret_value);
  if (!strlen(to) && !strcmp(t, "/")) {
    to = "./";
  }

  /* Strip trailing slashes */
  flen = strlen(from);
  if (flen > 1 && from[flen - 1] == '/') {
    const char* p = from + flen - 2;
    int n;

    while (*p == '/' && (p > from)) {
      p--;
    }
    n = p - from + 1;
    memcpy(newfrom, from, n);
    newfrom[n] = 0;
    from = newfrom;
  }

  if (file_size(to) == -2) {
    /* Target is a directory; build full target filename. */
    const char* cp;
    char newto[MAX_FNAME_SIZE + MAX_PATH_LEN + 2];

    cp = strrchr(from, '/');
    if (cp) {
      cp++;
    } else {
      cp = from;
    }

    sprintf(newto, "%s/%s", to, cp);
    return do_move(from, newto, flag);
  }
  return do_move(from, to, flag);
}
#endif /* F_RENAME */

int copy_file(const char* from, const char* to) {
  extern svalue_t apply_ret_value;

  from = check_valid_path(from, current_object, "move_file", 0);
  assign_svalue(&from_sv, &apply_ret_value);

  to = check_valid_path(to, current_object, "move_file", 1);
  assign_svalue(&to_sv, &apply_ret_value);

  if (from == nullptr) {
    return -1;
  }
  if (to == nullptr) {
    return -2;
  }

  if (lstat(from, &from_stats) != 0) {
    error("/%s: lstat failed\n", from);
    return 1;
  }
  if (lstat(to, &to_stats) == 0) {
#ifdef __WIN32
    if (!strcmp(from, to)) {
#else
    if (from_stats.st_dev == to_stats.st_dev && from_stats.st_ino == to_stats.st_ino) {
#endif
      error("`/%s' and `/%s' are the same file", from, to);
      return 1;
    }
  } else if (errno != ENOENT) {
    error("/%s: unknown error\n", to);
    return 1;
  }

  if (file_size(to) == -2) {
    /* Target is a directory; build full target filename. */
    const char* cp;
    char newto[MAX_FNAME_SIZE + MAX_PATH_LEN + 2];

    cp = strrchr(from, '/');
    if (cp) {
      cp++;
    } else {
      cp = from;
    }
    sprintf(newto, "%s/%s", to, cp);
    return copy_file(from, newto);
  }

  std::error_code error_code;
  auto base = fs::current_path();
  fs::copy_file(base / from, base / to, fs::copy_options::overwrite_existing, error_code);

  if (error_code) {
    debug_message("Error copying file from /%s to /%s, Error: %s", from, to,
                  error_code.message().c_str());
    return -1;
  }

  return 1;
}

#ifdef F_CP
void f_cp() {
  int i;

  i = copy_file(sp[-1].u.string, sp[0].u.string);
  free_string_svalue(sp--);
  free_string_svalue(sp);
  put_number(i);
}
#endif

#ifdef F_FILE_SIZE
void f_file_size() {
  LPC_INT i = file_size(sp->u.string);

  // cross platform fix
#ifdef _WIN32
  if (i == -1 && sp->u.string[SVALUE_STRLEN(sp) - 1] == '/') {
    auto len = SVALUE_STRLEN(sp);
    auto tmp = string_copy(sp->u.string, "f_file_size");
    tmp[len - 1] = '\0';
    if (file_size(tmp) == -2) {
      i = -2;
    }
    FREE_MSTR(tmp);
  }
#endif

  free_string_svalue(sp);
  put_number(i);
}
#endif

#ifdef F_GET_DIR
void f_get_dir() {
  array_t* vec;

  vec = get_dir((sp - 1)->u.string, sp->u.number);
  free_string_svalue(--sp);
  if (vec) {
    put_array(vec);
  } else {
    *sp = const0;
  }
}
#endif

#ifdef F_LINK
void f_link() {
  svalue_t *ret, *arg;
  int i;

  arg = sp;
  push_svalue(arg - 1);
  push_svalue(arg);
  ret = apply_master_ob(APPLY_VALID_LINK, 2);
  if (MASTER_APPROVED(ret, "valid_link")) {
    i = do_rename((sp - 1)->u.string, sp->u.string, F_LINK);
  } else {
    i = 0;
  }
  // Free both string arguments before replacing the slot with the
  // result -- the old epilogue abandoned them (two shared-string refs
  // leaked per call; caught by the testsuite's link test + leak gate).
  free_string_svalue(sp--);
  free_string_svalue(sp);
  put_number(i);
}
#endif /* F_LINK */

#ifdef F_MKDIR
void f_mkdir() {
  const char* path;

  path = check_valid_path(sp->u.string, current_object, "mkdir", 1);
  if (!path || OS_mkdir(path, 0770) == -1) {
    free_string_svalue(sp);
    *sp = const0;
  } else {
    free_string_svalue(sp);
    *sp = const1;
  }
}
#endif
