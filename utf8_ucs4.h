#include <wctype.h>

#undef __BEGIN_DECLS 
#undef __END_DECLS 

#ifdef __cplusplus 
# define __BEGIN_DECLS extern "C" { 
# define __END_DECLS } 
#else 
# define __BEGIN_DECLS
# define __END_DECLS
#endif 

__BEGIN_DECLS

/*
   Converts one character from utf8 to ucs4.
   'ptr'    - utf8 input
   'result' - ucs4 result
   Returns pointer to the next character if success or
      NULL otherwise
*/
const char * utf8_to_ucs4 (const char *ptr, wint_t *result);

/*
   Converts one character from ucs4 to utf8
   'ucs4'   - ucs4 input
   'result' - utf8 result (contain at most 7 'char's)
   Returns 1 if success, or 0 otherwise
*/
int ucs4_to_utf8 (wint_t ucs4, char *result);

/*
  Converts utf8 string to lower case.
  Returns 'str' if success, or NULL otherwise
*/
char *strlwr_utf8 (char *str);

/*
  Converts utf8 string to upper case.
  Returns 'str' if success, or NULL otherwise
*/
char *strupr_utf8 (char *str);

/*
  Converts 8bit string to lower case.
  Returns 'str'.
*/
char *strlwr_8bit (char *str);

/*
  Converts 8bit string to upper case.
  Returns 'str'.
*/
char *strupr_8bit (char *str);

__END_DECLS
