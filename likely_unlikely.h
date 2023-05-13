/* likely()/unlikely() macros for branch optimization
 * By Jody Bruchon <jody@jodybruchon.com>
 * Released to the public domain */

/* Un-define if already defined */
#if !defined NO_LIKELY_UNLIKELY && (defined __GNUC__ || defined __clang__)
#ifdef likely
#undef likely
#endif
#ifdef unlikely
#undef unlikely
#endif

#define likely(a) __builtin_expect((a), 1)
#define unlikely(a) __builtin_expect((a), 0)

#else /* no GCC/Clang */
#define likely(a) a
#define unlikely(a) a
#endif
