/*
 * Windows-native code for getting stat()-like information
 *
 * Copyright (C) 2016 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2/v3 (your choice)
 */

#include "win_stat.h"
#include <stdint.h>
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>

/* Get stat()-like extra information for a file on Windows */
int win_stat(const char * const restrict filename, struct winstat * const restrict buf)
{
  HANDLE hfile;
  BY_HANDLE_FILE_INFORMATION bhfi;

#ifdef UNICODE
  static wchar_t wname[PATH_MAX];
  if (!MultiByteToWideChar(CP_UTF8, 0, filename, -1, wname, PATH_MAX)) return -1;
  hfile = CreateFileW(wname, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		  FILE_FLAG_BACKUP_SEMANTICS, NULL);
#else
  hfile = CreateFile(filename, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		  FILE_FLAG_BACKUP_SEMANTICS, NULL);
#endif

  if (hfile == INVALID_HANDLE_VALUE) goto failure;
  if (!GetFileInformationByHandle(hfile, &bhfi)) goto failure;

  buf->inode = ((uint64_t)(bhfi.nFileIndexHigh) << 32) + (uint64_t)bhfi.nFileIndexLow;
  buf->size = ((uint64_t)(bhfi.nFileSizeHigh) << 32) + (uint64_t)bhfi.nFileSizeLow;
  buf->device = (uint32_t)bhfi.dwVolumeSerialNumber;
  buf->nlink = (uint32_t)bhfi.nNumberOfLinks;
  buf->mode = (uint32_t)bhfi.dwFileAttributes;

  CloseHandle(hfile);
  return 0;

failure:
  CloseHandle(hfile);
  return -1;
}
