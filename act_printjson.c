/* Print comprehensive information to stdout in JSON format
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include "jdupes.h"
#include "version.h"
#include "jody_win_unicode.h"
#include "act_printjson.h"

#define TO_HEX(a) (char)(((a) & 0x0f) <= 0x09 ? (a) + 0x30 : (a) + 0x57)

static void json_escape(char *string, char *escaped)
{
  int length = 0;
  while (*string != '\0' && length < (PATH_MAX * 2 - 1)) {
    switch (*string) {
      case '\"':
      case '\\':
        *escaped++ = '\\';
        *escaped++ = *string++;
        length += 2;
        break;
      default:
	if (*string < 0x20) {
	  strcpy(escaped, "\\u00");
	  escaped += 3;
	  *escaped++ = TO_HEX((*string >> 4));
	  *escaped++ = TO_HEX(*string++);
	  length += 5;
	} else {
          *escaped++ = *string++;
          length++;
	}
        break;
    }
  }
  *escaped = '\0';
  return;
}

extern void printjson(file_t * restrict files, const int argc, char **argv)
{
  file_t * restrict tmpfile;
  int arg = 0, comma = 0;
  char *temp = string_malloc(PATH_MAX * 2);
  char *temp2 = string_malloc(PATH_MAX * 2);
  char *temp_insert = temp;

  LOUD(fprintf(stderr, "act_printjson: %p\n", files));

  /* Output information about the jdupes command environment */
  printf("{\n  \"jdupesVersion\": \"%s\",\n  \"jdupesVersionDate\": \"%s\",\n", VER, VERDATE);
  
  printf("  \"commandLine\": \"");
  while (arg < argc) {
    sprintf(temp_insert, " %s", argv[arg]);
    temp_insert += strlen(temp_insert);
    arg++;
  }
  json_escape(temp, temp2);
  printf("%s\",\n", temp2);
  printf("  \"extensionFlags\": \"");
  if (extensions[0] == NULL) printf("none\",\n");
  else for (int c = 0; extensions[c] != NULL; c++)
    printf("%s%s", extensions[c], extensions[c+1] == NULL ? "\",\n" : " ");

  printf("  \"matchSets\": [\n");
  while (files != NULL) {
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      if (comma) printf(",\n");
      printf("    {\n      \"fileSize\": %" PRIdMAX ",\n      \"fileList\": [\n        { \"filePath\": \"", (intmax_t)files->size);
      sprintf(temp, "%s", files->d_name);
      json_escape(temp, temp2);
      fwprint(stdout, temp2, 0);
      printf("\"");
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
        printf(" },\n        { \"filePath\": \"");
        sprintf(temp, "%s", tmpfile->d_name);
        json_escape(temp, temp2);
        fwprint(stdout, temp2, 0);
        printf("\"");
        tmpfile = tmpfile->duplicates;
      }
      printf(" }\n      ]\n    }");
      comma = 1;
    }
    files = files->next;
  }

  printf("\n  ]\n}\n");

  string_free(temp); string_free(temp2);
  return;
}
