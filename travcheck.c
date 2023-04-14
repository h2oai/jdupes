/* jdupes double-traversal prevention tree
 * See jdupes.c for license information */

#ifndef NO_TRAVCHECK

#include <stdlib.h>
#include <inttypes.h>
#include "jdupes.h"
#include "travcheck.h"

/* Simple traversal balancing hash - scrambles inode number */
#define TRAVHASH(device,inode) (((inode << 55 | (inode >> 9)) + (device << 13)))

static struct travdone *travdone_head = NULL;

/* Create a new traversal check object and initialize its values */
static struct travdone *travdone_alloc(const dev_t device, const jdupes_ino_t inode, uintmax_t hash)
{
  struct travdone *trav;

  LOUD(fprintf(stderr, "travdone_alloc(dev %" PRIdMAX ", ino %" PRIdMAX ", hash %" PRIuMAX ")\n", (intmax_t)device, (intmax_t)inode, hash);)

  trav = (struct travdone *)jc_string_malloc(sizeof(struct travdone));
  if (trav == NULL) {
    LOUD(fprintf(stderr, "travdone_alloc: malloc failed\n");)
    return NULL;
  }
  trav->left = NULL;
  trav->right = NULL;
  trav->hash = hash;
  trav->device = device;
  trav->inode = inode;
  LOUD(fprintf(stderr, "travdone_alloc returned %p\n", (void *)trav);)
  return trav;
}


/* De-allocate the travdone tree */
void travdone_free(struct travdone *cur)
{
  LOUD(fprintf(stderr, "travdone_free(%p)\n", cur);)

  if (cur == NULL) {
    if (travdone_head == NULL) return;
    cur = travdone_head;
    travdone_head = NULL;
  }
  if (cur->left == cur) goto error_travdone_ptr;
  if (cur->right == cur) goto error_travdone_ptr;
  if (cur->left != NULL) travdone_free(cur->left);
  if (cur->right != NULL) travdone_free(cur->right);
  if (cur != NULL) jc_string_free(cur);
  return;
error_travdone_ptr:
  fprintf(stderr, "internal error: invalid pointer in travdone_free(), report this\n");
  exit(EXIT_FAILURE);
}


/* Check to see if device:inode pair has already been traversed */
int traverse_check(const dev_t device, const jdupes_ino_t inode)
{
  struct travdone *traverse = travdone_head;
  uintmax_t travhash;

  LOUD(fprintf(stderr, "traverse_check(dev %" PRIuMAX ", ino %" PRIuMAX "\n", (uintmax_t)device, (uintmax_t)inode);)
  travhash = TRAVHASH(device, inode);
  if (travdone_head == NULL) {
    travdone_head = travdone_alloc(device, inode, TRAVHASH(device, inode));
    if (travdone_head == NULL) return 2;
  } else {
    traverse = travdone_head;
    while (1) {
      if (traverse == NULL) jc_nullptr("traverse_check()");
      /* Don't re-traverse directories we've already seen */
      if (inode == traverse->inode && device == traverse->device) {
        LOUD(fprintf(stderr, "traverse_check: already seen: %" PRIuMAX ":%" PRIuMAX "\n", (uintmax_t)device, (uintmax_t)inode);)
        return 1;
      } else {
        if (travhash > traverse->hash) {
          /* Traverse right */
          if (traverse->right == NULL) {
            LOUD(fprintf(stderr, "traverse_check add right: %" PRIuMAX ", %" PRIuMAX"\n", (uintmax_t)device, (uintmax_t)inode);)
            DBG(travdone_rights++);
            traverse->right = travdone_alloc(device, inode, travhash);
            if (traverse->right == NULL) return 2;
            break;
          }
          traverse = traverse->right;
          continue;
        } else {
          /* Traverse left */
          if (traverse->left == NULL) {
            LOUD(fprintf(stderr, "traverse_check add left: %" PRIuMAX ", %" PRIuMAX "\n", (uintmax_t)device, (uintmax_t)inode);)
            DBG(travdone_lefts++);
            traverse->left = travdone_alloc(device, inode, travhash);
            if (traverse->left == NULL) return 2;
            break;
          }
          traverse = traverse->left;
          continue;
        }
      }
    }
  }
  return 0;
}
#endif /* NO_TRAVCHECK */
