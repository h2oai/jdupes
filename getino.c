/*
 * Win32 "inode number" generator
 *
 * Code taken from http://gnuwin32.sourceforge.net/compile.html
 *
 * This code was an example and did not come with a specific license, but
 * http://gnuwin32.sourceforge.net/license.html seems to imply that the code
 * carries no restrictions at all: "There are no royalties or other fees to
 * be paid for use of software from GnuWin, and GnuWin does not impose
 * licenses other than those that hold for the original sources."
 *
 * WARNING: Only works on local filesystems, not network drives!
 */

#include <sys/stat.h>
#include <io.h>
#include <stdint.h>
#include <windows.h>
#define LODWORD(l) ((DWORD)((DWORDLONG)(l)))
#define HIDWORD(l) ((DWORD)(((DWORDLONG)(l)>>32)&0xFFFFFFFF))
#define MAKEDWORDLONG(a,b) ((DWORDLONG)(((DWORD)(a))|(((DWORDLONG)((DWORD)(b)))<<32)))

#define INOSIZE (8 * sizeof(ino_t))
#define SEQNUMSIZE (16)

ino_t getino (char *path)
{
	BY_HANDLE_FILE_INFORMATION FileInformation;
	HANDLE hFile;
	uint64_t ino64, refnum;
	ino_t ino;

	if (!path || !*path) /* path = NULL */
		return 0;
	if (access (path, F_OK)) /* path does not exist */
		return -1;

	/* obtain handle to "path"; FILE_FLAG_BACKUP_SEMANTICS is used to open directories */
	hFile = CreateFile (path, 0, 0, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_READONLY,
			NULL);

	if (hFile == INVALID_HANDLE_VALUE) /* file cannot be opened */
		return 0;

	ZeroMemory (&FileInformation, sizeof(FileInformation));

	if (!GetFileInformationByHandle (hFile, &FileInformation)) { /* cannot obtain FileInformation */
		CloseHandle (hFile);
		return 0;
	}

	ino64 = (uint64_t) MAKEDWORDLONG (
		FileInformation.nFileIndexLow, FileInformation.nFileIndexHigh);

	refnum = ino64 & ((~(0ULL)) >> SEQNUMSIZE); /* strip sequence number */

	/* transform 64-bits ino into 16-bits by hashing */
	ino = (ino_t) (
			( (LODWORD(refnum)) ^ ((LODWORD(refnum)) >> INOSIZE) )
		^
			( (HIDWORD(refnum)) ^ ((HIDWORD(refnum)) >> INOSIZE) )
		);

	CloseHandle (hFile);

	return ino;
}
