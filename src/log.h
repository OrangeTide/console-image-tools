/*
 * logging macros
 */
#ifndef LOG_H
#define LOG_H
#ifndef NDEBUG
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#define TRACE(fmt, ...) fprintf(stderr, "%s:%u:%s():" fmt, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define PERROR(msg) TRACE("%s:%s(errno=%d)", msg, strerror(errno), errno)
#else
#define DEBUG 
#define TRACE 
#define PERROR perror
#endif

#define TRACE_MSG(msg) TRACE("%s\n", msg)
#define ERROR_MSG(msg) fprintf(stderr, "%s\n", msg)
#endif
