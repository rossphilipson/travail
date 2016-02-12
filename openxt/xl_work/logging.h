#ifndef LOGGING_H_
# define LOGGING_H_

# include <stdarg.h>
# include <stdio.h>

/*
#ifdef printf
# undef printf
#endif
#ifdef vfprintf
# undef vfprintf
#endif
#ifdef fprintf
# undef fprintf
#endif

# define printf(...) qemu_log_printf(__VA_ARGS__)
# define vfprintf(...) qemu_log_vfprintf(__VA_ARGS__)
# define fprintf(...) qemu_log_fprintf(__VA_ARGS__)*/

void logging_set_prefix(const char *ident);
int qemu_log_vfprintf(FILE *stream, const char *format, va_list ap);
int qemu_log_printf(const char *format, ...)
  __attribute__ ((format (printf, 1, 2)));
int qemu_log_fprintf(FILE *stream, const char *format, ...)
  __attribute__ ((format (printf, 2, 3)));


#endif /* !LOGGING_H_ */
