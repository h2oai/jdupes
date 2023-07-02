/* libjodycode version checks - user-defined requirements
 *
 * Edit this file to match your libjodycode API/feature level requirements
 *
 * Copyright (C) 2023 by Jody Bruchon <jody@jodybruchon.com>
 * Licensed under The MIT License */

/* Minimum libjodycode feature level required
 * You can copy this from the current libjodycode feature level; however,
 * the ideal number is the lowest number that is still compatible. For
 * example: in level 1 the alarm API sets the "ring" to 1 like a flag
 * while the level 2 alarm API increments "ring" when triggered to tell
 * the application how many alarms have been triggered since reset. For
 * applications that don't care about the number of alarms or that work
 * properly without that info, level 1 minimum is fine; those that rely
 * on the newer behavior must specify a minimum level of 2. */
#define MY_FEATURELEVEL_REQ 1

/* API sub-version requirements
 * For any libjodycode API you use, copy its number from libjodycode.h to
 * this list.
 * To indicate you don't use an API, set it to 0.
 * To auto-fill the API numbers you're building against, set it to 255.
 * Any number not matching libjodycode will cause an error exit. */
#define MY_CACHEINFO_REQ   255
#define MY_JODY_HASH_REQ   255
#define MY_OOM_REQ         255
#define MY_PATHS_REQ       255
#define MY_SIZE_SUFFIX_REQ 255
#define MY_SORT_REQ        255
#define MY_STRING_REQ      255
#define MY_STRTOEPOCH_REQ  255
#define MY_WIN_STAT_REQ    255
#define MY_WIN_UNICODE_REQ 255
#define MY_ERROR_REQ       255
#define MY_ALARM_REQ       255
