/* jdupes double-traversal prevention tree
 * See jdupes.c for license information */

#ifndef JDUPES_TRAVCHECK_H
#define JDUPES_TRAVCHECK_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NO_TRAVCHECK

/* Tree to track each directory traversed */
struct travdone {
  struct travdone *left;
  struct travdone *right;
  uintmax_t hash;
  jdupes_ino_t inode;
  dev_t device;
};

/* De-allocate the travdone tree */
void travdone_free(struct travdone *cur);
int traverse_check(const dev_t device, const jdupes_ino_t inode);

#endif /* NO_TRAVCHECK */

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_TRAVCHECK_H */
