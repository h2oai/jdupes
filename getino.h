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

#ifndef GETINO_H
#define GETINO_H

ino_t getino (char *path);

#endif /* GETINO_H */
