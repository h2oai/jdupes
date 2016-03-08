/*
 * Windows-native code for getting stat()-like information
 *
 * Copyright (C) 2016 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License or GNU GPL v2/v3 (your choice)
 */

#include <stdint.h>
#include "win_stat.h"
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
/* Get the hard link count for a file on Windows */
int win_stat(const char * const restrict filename, struct winstat * const restrict buf)
{
  HANDLE hfile;
  BY_HANDLE_FILE_INFORMATION bhfi;

  hfile = CreateFile(filename, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		  FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (hfile == INVALID_HANDLE_VALUE) {
	  fprintf(stderr, "Invalid handle value: %s (0x%x)\n", filename, (int)GetLastError());
	  goto failure;
  }

  if (!GetFileInformationByHandle(hfile, &bhfi)) {
	  fprintf(stderr, "Error getting file information: %s, 0x%x\n", filename, (int)GetLastError());
	  goto failure;
  }

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
