#ifndef _UTIL_H
#define _UTIL_H

/* Hash Type Dict */
struct dictType wstrDictType;
struct configuration;

void nonBlockCloseOnExecPipe(int *fd0, int *fd1);

/* Configuration Validator */
int stringValidator(struct configuration *conf, const char *key, const char *val);
int unsignedIntValidator(struct configuration *conf, const char *key, const char *val);
int enumValidator(struct configuration *conf, const char *key, const char *val);
int boolValidator(struct configuration *conf, const char *key, const char *val);

/* Assert */
void wheat_stacktrace(int skip_count);
void wheat_assert(const char *cond, const char *file, int line, int panic);

#define ASSERT(_x) do {                            \
    if (!(_x)) {                                   \
        wheat_assert(#_x, __FILE__, __LINE__, 1);  \
    }                                              \
} while (0)

#endif
