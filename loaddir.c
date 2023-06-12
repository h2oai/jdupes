/* jdupes directory scanning code
 * This file is part of jdupes; see jdupes.c for license information */

#include <dirent.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <libjodycode.h>
#include "likely_unlikely.h"
#include "jdupes.h"
#include "checks.h"
#include "filestat.h"
#include "progress.h"
#include "interrupt.h"
#ifndef NO_TRAVCHECK
 #include "travcheck.h"
#endif

#ifdef UNICODE
 static wpath_t wname;
#endif

/* Detect Windows and modify as needed */
#if defined _WIN32 || defined __MINGW32__
 const char dir_sep = '\\';
#else /* Not Windows */
 const char dir_sep = '/';
#endif /* _WIN32 || __MINGW32__ */

static file_t *init_newfile(const size_t len, file_t * restrict * const restrict filelistp)
{
  file_t * const restrict newfile = (file_t *)malloc(sizeof(file_t));

  if (unlikely(!newfile)) jc_oom("init_newfile() file structure");
  if (unlikely(!filelistp)) jc_nullptr("init_newfile() filelistp");

  LOUD(fprintf(stderr, "init_newfile(len %" PRIuMAX ", filelistp %p)\n", (uintmax_t)len, filelistp));

  memset(newfile, 0, sizeof(file_t));
  newfile->d_name = (char *)malloc(len);
  if (!newfile->d_name) jc_oom("init_newfile() filename");

  newfile->next = *filelistp;
#ifndef NO_USER_ORDER
  newfile->user_order = user_item_count;
#endif
  newfile->size = -1;
  newfile->duplicates = NULL;
  return newfile;
}


/* This is disabled until a check is in place to make it safe */
#if 0
/* Add a single file to the file tree */
file_t *grokfile(const char * const restrict name, file_t * restrict * const restrict filelistp)
{
  file_t * restrict newfile;

  if (!name || !filelistp) jc_nullptr("grokfile()");
  LOUD(fprintf(stderr, "grokfile: '%s' %p\n", name, filelistp));

  /* Allocate the file_t and the d_name entries */
  newfile = init_newfile(strlen(name) + 2, filelistp);

  strcpy(newfile->d_name, name);

  /* Single-file [l]stat() and exclusion condition check */
  if (check_singlefile(newfile) != 0) {
    LOUD(fprintf(stderr, "grokfile: check_singlefile rejected file\n"));
    free(newfile->d_name);
    free(newfile);
    return NULL;
  }
  return newfile;
}
#endif

/* Load a directory's contents into the file tree, recursing as needed */
void loaddir(const char * const restrict dir,
                file_t * restrict * const restrict filelistp,
                int recurse)
{
  file_t * restrict newfile;
  struct dirent *dirinfo;
  size_t dirlen;
  static uint_fast32_t loaddir_level = 0;
  int i, single = 0;
  jdupes_ino_t inode;
  dev_t device, n_device;
  jdupes_mode_t mode;
#ifdef UNICODE
  WIN32_FIND_DATA ffd;
  HANDLE hFind = INVALID_HANDLE_VALUE;
  char *p;
#else
  DIR *cd;
#endif
  static int sf_warning = 0; /* single file warning should only appear once */

  if (unlikely(dir == NULL || filelistp == NULL)) jc_nullptr("loaddir()");
  LOUD(fprintf(stderr, "loaddir: scanning '%s' (order %d, recurse %d)\n", dir, user_item_count, recurse));

  /* Get directory stats (or file stats if it's a file) */
  i = getdirstats(dir, &inode, &device, &mode);
  if (unlikely(i < 0)) goto error_stat_dir;
  /* if dir is actually a file, just add it to the file tree */
  if (i == 1) {
/* Single file addition is disabled for now because there is no safeguard
 * against the file being compared against itself if it's added in both a
 * recursion and explicitly on the command line. */
#if 0
    LOUD(fprintf(stderr, "loaddir -> grokfile '%s'\n", dir));
    newfile = grokfile(dir, filelistp);
    if (newfile == NULL) {
      LOUD(fprintf(stderr, "grokfile rejected '%s'\n", dir));
      return;
    }
    single = 1;
    goto add_single_file;
#endif
    if (sf_warning == 0) {
      fprintf(stderr, "\nFile specs on command line disabled in this version for safety\n");
      fprintf(stderr, "This should be restored (and safe) in a future release\n");
      fprintf(stderr, "See https://github.com/jbruchon/jdupes or email jody@jodybruchon.com\n");
      sf_warning = 1;
    }
    return; /* Remove when single file is restored */
  }

/* Double traversal prevention tree */
#ifndef NO_TRAVCHECK
  if (likely(!ISFLAG(flags, F_NOTRAVCHECK))) {
    i = traverse_check(device, inode);
    if (unlikely(i == 1)) return;
    if (unlikely(i == 2)) goto error_stat_dir;
  }
#endif /* NO_TRAVCHECK */

  item_progress++;
  loaddir_level++;

#ifdef UNICODE
  /* Windows requires \* at the end of directory names */
  strncpy(tempname, dir, PATHBUF_SIZE * 2 - 1);
  dirlen = strlen(tempname) - 1;
  p = tempname + dirlen;
  if (*p == '/' || *p == '\\') *p = '\0';
  strncat(tempname, "\\*", PATHBUF_SIZE * 2 - 1);

  if (unlikely(!M2W(tempname, wname))) goto error_cd;

  LOUD(fprintf(stderr, "FindFirstFile: %s\n", dir));
  hFind = FindFirstFileW(wname, &ffd);
  if (unlikely(hFind == INVALID_HANDLE_VALUE)) { LOUD(fprintf(stderr, "\nfile handle bad\n")); goto error_cd; }
  LOUD(fprintf(stderr, "Loop start\n"));
  do {
    char * restrict tp = tempname;
    size_t d_name_len;

    /* Get necessary length and allocate d_name */
    dirinfo = (struct dirent *)malloc(sizeof(struct dirent));
    if (!W2M(ffd.cFileName, dirinfo->d_name)) continue;
#else
  cd = opendir(dir);
  if (unlikely(!cd)) goto error_cd;

  while ((dirinfo = readdir(cd)) != NULL) {
    char * restrict tp = tempname;
    size_t d_name_len;
#endif /* UNICODE */

    LOUD(fprintf(stderr, "loaddir: readdir: '%s'\n", dirinfo->d_name));
    if (unlikely(!jc_streq(dirinfo->d_name, ".") || !jc_streq(dirinfo->d_name, ".."))) continue;
    check_sigusr1();
    if (progress_alarm != 0) {
      progress_alarm = 0;
      update_phase1_progress("dirs");
    }

    /* Assemble the file's full path name, optimized to avoid strcat() */
    dirlen = strlen(dir);
    d_name_len = strlen(dirinfo->d_name);
    memcpy(tp, dir, dirlen+1);
    if (dirlen != 0 && tp[dirlen-1] != dir_sep) {
      tp[dirlen] = dir_sep;
      dirlen++;
    }
    if (unlikely(dirlen + d_name_len + 1 >= (PATHBUF_SIZE * 2))) goto error_overflow;
    tp += dirlen;
    memcpy(tp, dirinfo->d_name, d_name_len);
    tp += d_name_len;
    *tp = '\0';
    d_name_len++;

    /* Allocate the file_t and the d_name entries */
    newfile = init_newfile(dirlen + d_name_len + 2, filelistp);

    tp = tempname;
    memcpy(newfile->d_name, tp, dirlen + d_name_len);

    /*** WARNING: tempname global gets reused by check_singlefile here! ***/

    /* Single-file [l]stat() and exclusion condition check */
    if (check_singlefile(newfile) != 0) {
      LOUD(fprintf(stderr, "loaddir: check_singlefile rejected file\n"));
      free(newfile->d_name);
      free(newfile);
      continue;
    }

    /* Optionally recurse directories, including symlinked ones if requested */
    if (S_ISDIR(newfile->mode)) {
      if (recurse) {
        /* --one-file-system - WARNING: this clobbers inode/mode */
        if (ISFLAG(flags, F_ONEFS)
            && (getdirstats(newfile->d_name, &inode, &n_device, &mode) == 0)
            && (device != n_device)) {
          LOUD(fprintf(stderr, "loaddir: directory: not recursing (--one-file-system)\n"));
          free(newfile->d_name);
          free(newfile);
          continue;
        }
#ifndef NO_SYMLINKS
        else if (ISFLAG(flags, F_FOLLOWLINKS) || !ISFLAG(newfile->flags, FF_IS_SYMLINK)) {
          LOUD(fprintf(stderr, "loaddir: directory(symlink): recursing (-r/-R)\n"));
          loaddir(newfile->d_name, filelistp, recurse);
        }
#else
        else {
          LOUD(fprintf(stderr, "loaddir: directory: recursing (-r/-R)\n"));
          loaddir(newfile->d_name, filelistp, recurse);
        }
#endif
      } else { LOUD(fprintf(stderr, "loaddir: directory: not recursing\n")); }
      free(newfile->d_name);
      free(newfile);
      continue;
    } else {
//add_single_file:
      /* Add regular files to list, including symlink targets if requested */
#ifndef NO_SYMLINKS
      if (!ISFLAG(newfile->flags, FF_IS_SYMLINK) || (ISFLAG(newfile->flags, FF_IS_SYMLINK) && ISFLAG(flags, F_FOLLOWLINKS))) {
#else
      if (S_ISREG(newfile->mode)) {
#endif
        *filelistp = newfile;
        filecount++;
        progress++;

      } else {
        LOUD(fprintf(stderr, "loaddir: not a regular file: %s\n", newfile->d_name);)
        free(newfile->d_name);
        free(newfile);
        if (single == 1) {
          single = 0;
          goto skip_single;
        }
        continue;
      }
    }
    /* Skip directory stuff if adding only a single file */
    if (single == 1) {
      single = 0;
      goto skip_single;
    }
  }

#ifdef UNICODE
  while (FindNextFileW(hFind, &ffd) != 0);
  FindClose(hFind);
#else
  closedir(cd);
#endif

skip_single:
  loaddir_level--;
  if (progress_alarm != 0) {
    progress_alarm = 0;
    if (loaddir_level == 0) update_phase1_progress("items");
  }
  return;

error_stat_dir:
  fprintf(stderr, "\ncould not stat dir "); jc_fwprint(stderr, dir, 1);
  return;
error_cd:
  fprintf(stderr, "\ncould not chdir to "); jc_fwprint(stderr, dir, 1);
  return;
error_overflow:
  fprintf(stderr, "\nerror: a path buffer overflowed\n");
  exit(EXIT_FAILURE);
}
