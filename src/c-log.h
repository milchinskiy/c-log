// c-log.h — tiny, no-alloc, (optionally) thread-safe logger with colors,
// file:line, groups & timers. Single-header: include everywhere; in ONE .c file
// #define CLOG_IMPLEMENTATION before including.
//
// https://github.com/milchinskiy/c-log
// MIT License

#ifndef CLOG_H
#define CLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#if !defined(CLOG_THREAD_SAFE)
#    define CLOG_THREAD_SAFE 1
#endif
#if !defined(CLOG_LINE_MAX)
#    define CLOG_LINE_MAX 1024
#endif
#if !defined(CLOG_TIMERS_MAX)
#    define CLOG_TIMERS_MAX 16
#endif
#if !defined(CLOG_COLOR)
#    define CLOG_COLOR 1
#endif
#if !defined(CLOG_COLOR_FORCE)
#    define CLOG_COLOR_FORCE 0
#endif
#if !defined(CLOG_WITH_LINE)
#    define CLOG_WITH_LINE 1
#endif
#if !defined(CLOG_WITH_TID)
#    define CLOG_WITH_TID 1
#endif
#if !defined(CLOG_WITH_BUILD_IN_PREFIX)
#    define CLOG_WITH_BUILD_IN_PREFIX 0
#endif
#if !defined(CLOG_TID_SHORT)
#    define CLOG_TID_SHORT 0  // 1 => print low 24 bits hex as (t#XXXXXX)
#endif
#if !defined(CLOG_TIME_UTC)
#    define CLOG_TIME_UTC 0
#endif
#if !defined(CLOG_TIMER_NS_MAX)
#    define CLOG_TIMER_NS_MAX 1000ULL       /* < 1 µs  -> ns */
#endif
#if !defined(CLOG_TIMER_US_MAX)
#    define CLOG_TIMER_US_MAX 1000000ULL    /* < 1 ms  -> µs */
#endif
#if !defined(CLOG_TIMER_MS_MAX)
#    define CLOG_TIMER_MS_MAX 1000000000ULL /* < 1 s   -> ms, else s */
#endif
/* UTF-8 micro sign by default; override with -DCLOG_TIMER_UNIT_US="\"us\"" if needed */
#if !defined(CLOG_TIMER_UNIT_US)
#    define CLOG_TIMER_UNIT_US "µs"
#endif

// printf-style format checking
#if defined(__GNUC__) || defined(__clang__)
#    define CLOG_PRINTF(A, B) __attribute__((format(printf, A, B)))
#else
#    define CLOG_PRINTF(A, B)
#endif

#if !defined(CLOG_LOCK_KIND)
// 0 = none (not safe), 1 = spin (atomic_flag), 2 = mutex (pthread/SRWLOCK)
#    define CLOG_LOCK_KIND 2
#endif
#if !defined(CLOG_SPIN_ITERS)
#    define CLOG_SPIN_ITERS 100  // bounded spin before yielding (only for KIND=1)
#endif

// -------- platform ----------
#if defined(_WIN32)
#    include <fcntl.h>
#    include <io.h>
#    include <windows.h>
#    define CLOG_WRITE       _write
#    define CLOG_FD_STDERR   2
#    define CLOG_THREADLOCAL __declspec(thread)
static inline unsigned long clog_tid_(void) { return (unsigned long)GetCurrentThreadId(); }
static inline void          clog_localtime_parts_(int *Y, int *m, int *d, int *H, int *M, int *S, int *ms) {
    SYSTEMTIME st;
#    if CLOG_TIME_UTC
    GetSystemTime(&st);
#    else
    GetLocalTime(&st);
#    endif
    *Y  = st.wYear;
    *m  = st.wMonth;
    *d  = st.wDay;
    *H  = st.wHour;
    *M  = st.wMinute;
    *S  = st.wSecond;
    *ms = st.wMilliseconds;
}
static inline uint64_t clog_now_ns_mono_(void) {
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER        c;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    long double ticks = (long double)c.QuadPart, per_s = (long double)freq.QuadPart;
    long double ns = (ticks * 1000000000.0L) / per_s;
    return (uint64_t)ns;
}
#    if defined(CLOG_IMPLEMENTATION) && CLOG_COLOR && !CLOG_COLOR_FORCE
static inline int clog_isatty_fd_(int fd) {
    if (fd < 0) return 0;
    if (!_isatty(fd)) return 0;
    intptr_t osfh = _get_osfhandle(fd);
    if (osfh == -1) return 1;
    HANDLE h = (HANDLE)osfh;
    DWORD  mode;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    return 1;
}
#    endif
#else
#    include <fcntl.h>
#    include <pthread.h>
#    include <sys/stat.h>
#    include <sys/time.h>
#    include <sys/types.h>
#    include <unistd.h>
#    if defined(__linux__)
#        include <sys/syscall.h>
#    endif
#    define CLOG_WRITE       write
#    define CLOG_FD_STDERR   2
#    define CLOG_THREADLOCAL _Thread_local
static inline unsigned long clog_tid_(void) {
#    if defined(__linux__)
    return (unsigned long)syscall(SYS_gettid);
#    elif defined(__APPLE__)
    uint64_t tid = 0;
    (void)pthread_threadid_np(NULL, &tid);
    return (unsigned long)tid;
#    else
    return (unsigned long)(uintptr_t)pthread_self();
#    endif
}
static inline void clog_localtime_parts_(int *Y, int *m, int *d, int *H, int *M, int *S, int *ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t    sec = ts.tv_sec;
    struct tm tmv;
#    if CLOG_TIME_UTC
    gmtime_r(&sec, &tmv);
#    else
    localtime_r(&sec, &tmv);
#    endif
    *Y  = tmv.tm_year + 1900;
    *m  = tmv.tm_mon + 1;
    *d  = tmv.tm_mday;
    *H  = tmv.tm_hour;
    *M  = tmv.tm_min;
    *S  = tmv.tm_sec;
    *ms = (int)(ts.tv_nsec / 1000000);
}
static inline uint64_t clog_now_ns_mono_(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#    if defined(CLOG_IMPLEMENTATION) && CLOG_COLOR && !CLOG_COLOR_FORCE
static inline int clog_isatty_fd_(int fd) { return isatty(fd); }
#    endif
#endif

// ---------- Levels ----------
typedef enum { CLOG_TRACE = 0, CLOG_DEBUG, CLOG_INFO, CLOG_WARN, CLOG_ERROR, CLOG_FATAL } clog_level;

// Runtime default level (initial threshold)
#ifndef CLOG_DEFAULT_LEVEL
#    ifdef CLOG_LEVEL
#        define CLOG_DEFAULT_LEVEL CLOG_LEVEL
#    else
#        define CLOG_DEFAULT_LEVEL CLOG_INFO
#    endif
#endif

// Compile-time elision (removes code below this level)
#ifndef CLOG_COMPILETIME_MIN_LEVEL
#    ifdef CLOG_MIN_LEVEL
#        define CLOG_COMPILETIME_MIN_LEVEL CLOG_MIN_LEVEL
#    else
#        define CLOG_COMPILETIME_MIN_LEVEL CLOG_TRACE
#    endif
#endif

// ---------- API (public) ----------
void       clog_set_level(clog_level lvl);
clog_level clog_get_level(void);

int        clog_get_fd(void);
void       clog_set_fd(int fd);

// timers — call-site aware wrappers
void clogp_timer_start_(const char *file, int line, const char *label);
void clogp_timer_end_(const char *file, int line, const char *label);
#define clog_start_time(label) clogp_timer_start_(__FILE__, __LINE__, (label))
#define clog_end_time(label)   clogp_timer_end_(__FILE__, __LINE__, (label))

void clog_banner(void);

// internal front-ends
void clog_log_file_line_(
    clog_level lvl, const char *file, int line, const char *group, const char *fmt, ...
) CLOG_PRINTF(5, 6);
void clog_vlog_file_line_(clog_level lvl, const char *file, int line, const char *group, const char *fmt, va_list ap);

#if CLOG_COMPILETIME_MIN_LEVEL <= CLOG_TRACE
#    define log_trace(...)          clog_log_file_line_(CLOG_TRACE, __FILE__, __LINE__, NULL, __VA_ARGS__)
#    define log_trace_group(g, ...) clog_log_file_line_(CLOG_TRACE, __FILE__, __LINE__, (g), __VA_ARGS__)
#else
#    define log_trace(...)          ((void)0)
#    define log_trace_group(g, ...) ((void)0)
#endif

#if CLOG_COMPILETIME_MIN_LEVEL <= CLOG_DEBUG
#    define log_debug(...)          clog_log_file_line_(CLOG_DEBUG, __FILE__, __LINE__, NULL, __VA_ARGS__)
#    define log_debug_group(g, ...) clog_log_file_line_(CLOG_DEBUG, __FILE__, __LINE__, (g), __VA_ARGS__)
#else
#    define log_debug(...)          ((void)0)
#    define log_debug_group(g, ...) ((void)0)
#endif

#if CLOG_COMPILETIME_MIN_LEVEL <= CLOG_INFO
#    define log_info(...)          clog_log_file_line_(CLOG_INFO, __FILE__, __LINE__, NULL, __VA_ARGS__)
#    define log_info_group(g, ...) clog_log_file_line_(CLOG_INFO, __FILE__, __LINE__, (g), __VA_ARGS__)
#else
#    define log_info(...)          ((void)0)
#    define log_info_group(g, ...) ((void)0)
#endif

#if CLOG_COMPILETIME_MIN_LEVEL <= CLOG_WARN
#    define log_warn(...)          clog_log_file_line_(CLOG_WARN, __FILE__, __LINE__, NULL, __VA_ARGS__)
#    define log_warn_group(g, ...) clog_log_file_line_(CLOG_WARN, __FILE__, __LINE__, (g), __VA_ARGS__)
#else
#    define log_warn(...)          ((void)0)
#    define log_warn_group(g, ...) ((void)0)
#endif

#if CLOG_COMPILETIME_MIN_LEVEL <= CLOG_ERROR
#    define log_error(...)          clog_log_file_line_(CLOG_ERROR, __FILE__, __LINE__, NULL, __VA_ARGS__)
#    define log_error_group(g, ...) clog_log_file_line_(CLOG_ERROR, __FILE__, __LINE__, (g), __VA_ARGS__)
#else
#    define log_error(...)          ((void)0)
#    define log_error_group(g, ...) ((void)0)
#endif

#if CLOG_COMPILETIME_MIN_LEVEL <= CLOG_FATAL
#    define log_fatal(...)          clog_log_file_line_(CLOG_FATAL, __FILE__, __LINE__, NULL, __VA_ARGS__)
#    define log_fatal_group(g, ...) clog_log_file_line_(CLOG_FATAL, __FILE__, __LINE__, (g), __VA_ARGS__)
#else
#    define log_fatal(...)          ((void)0)
#    define log_fatal_group(g, ...) ((void)0)
#endif

// Scope timer helper (times a block; emits at DEBUG)
#define CLOG_CAT_(a, b) a##b
#define CLOG_CAT(a, b)  CLOG_CAT_(a, b)
#define CLOG_SCOPE_TIME(label)                                                                                \
    for (int CLOG_CAT(_clog_once_, __LINE__) = (clog_start_time(label), 0); !CLOG_CAT(_clog_once_, __LINE__); \
         (clog_end_time(label), CLOG_CAT(_clog_once_, __LINE__) = 1))

#ifdef CLOG_IMPLEMENTATION
#    include <errno.h>
#    include <string.h>

// --- Atomics shim for state (dedupe) ---
#    ifndef CLOG_HAVE_ATOMICS
#        if defined(__cplusplus)
#            define CLOG_HAVE_ATOMICS 1 /* use <atomic> in C++ */
#        elif !defined(__STDC_NO_ATOMICS__) && (defined(__GNUC__) || defined(__clang__))
#            define CLOG_HAVE_ATOMICS 1 /* C11 atomics on GCC/Clang */
#        else
#            define CLOG_HAVE_ATOMICS 0
#        endif
#    endif

#    if CLOG_HAVE_ATOMICS
#        if defined(__cplusplus)
/* --- C++ path: use <atomic> --- */
#            include <atomic>
using clog_atomic_int_ = std::atomic<int>;
#            define CLOG_STATE_INT(name, init)                                                             \
                static clog_atomic_int_ name{(init)};                                                      \
                static inline int       name##_load(void) { return name.load(std::memory_order_relaxed); } \
                static inline void      name##_store(int v) { name.store(v, std::memory_order_relaxed); }
#        else
/* --- C11 path: use <stdatomic.h> --- */
#            include <stdatomic.h>
/* Some libcs don’t define ATOMIC_VAR_INIT; identity fallback is correct for constants */
#            ifndef ATOMIC_VAR_INIT
#                define ATOMIC_VAR_INIT(x) (x)
#            endif
typedef atomic_int clog_atomic_int_;
#            define CLOG_STATE_INT(name, init)                                                                       \
                static clog_atomic_int_ name = ATOMIC_VAR_INIT(init);                                                \
                static inline int  name##_load(void) { return atomic_load_explicit(&(name), memory_order_relaxed); } \
                static inline void name##_store(int v) { atomic_store_explicit(&(name), v, memory_order_relaxed); }
#        endif
#    else
/* --- Fallback: non-atomic ints --- */
#        define CLOG_STATE_INT(name, init)                        \
            static int         name = (init);                     \
            static inline int  name##_load(void) { return name; } \
            static inline void name##_store(int v) { name = v; }
#    endif

CLOG_STATE_INT(g_lvl, CLOG_DEFAULT_LEVEL)
CLOG_STATE_INT(g_fd, CLOG_FD_STDERR)

static inline int  clog_lvl_load_(void) { return g_lvl_load(); }
static inline void clog_lvl_store_(int v) { g_lvl_store(v); }
static inline int  clog_fd_load_(void) { return g_fd_load(); }
static inline void clog_fd_store_(int v) { g_fd_store(v); }

#    if CLOG_THREAD_SAFE
#        if CLOG_LOCK_KIND == 1        /* Spinlock (bounded) */
#            if !CLOG_HAVE_ATOMICS
#                include <stdatomic.h> /* in case user disabled state atomics */
#            endif
#            if !defined(_WIN32)
#                include <sched.h>
#            endif
static atomic_flag g_lock = ATOMIC_FLAG_INIT;
static inline void clog_lock_(void) {
    int spins = 0;
    while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) {
        if (++spins >= CLOG_SPIN_ITERS) {
            spins = 0;
#            if defined(_WIN32)
            SwitchToThread();
#            else
            sched_yield();
#            endif
        }
    }
}
static inline void clog_unlock_(void) { atomic_flag_clear_explicit(&g_lock, memory_order_release); }
#        elif CLOG_LOCK_KIND == 2 /* Mutex (recommended) */
#            if defined(_WIN32)
static SRWLOCK     g_lock = SRWLOCK_INIT;
static inline void clog_lock_(void) { AcquireSRWLockExclusive(&g_lock); }
static inline void clog_unlock_(void) { ReleaseSRWLockExclusive(&g_lock); }
#            else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static inline void     clog_lock_(void) { (void)pthread_mutex_lock(&g_lock); }
static inline void     clog_unlock_(void) { (void)pthread_mutex_unlock(&g_lock); }
#            endif
#        else /* No locking */
static inline void clog_lock_(void) {}
static inline void clog_unlock_(void) {}
#        endif
#    else
static inline void clog_lock_(void) {}
static inline void clog_unlock_(void) {}
#    endif

// Per-thread scratch (no heap)
static CLOG_THREADLOCAL char g_buf[CLOG_LINE_MAX];

// Timers (per-thread fixed slots)
typedef struct {
    uint64_t key;
    uint64_t t0;
    bool     used;
} clog_timer_slot_;
static CLOG_THREADLOCAL clog_timer_slot_ g_timers[CLOG_TIMERS_MAX];

// Colors (compile out when disabled)
#    if CLOG_COLOR
#        include <stdlib.h>
#        define CLOG_ANSI_RESET "\x1b[0m"
static inline const char *clog_level_color_(clog_level l) {
    switch (l) {
        case CLOG_TRACE: return "\x1b[90m";
        case CLOG_DEBUG: return "\x1b[36m";
        case CLOG_INFO:  return "\x1b[32m";
        case CLOG_WARN:  return "\x1b[33m";
        case CLOG_ERROR: return "\x1b[31m";
        case CLOG_FATAL: return "\x1b[35m";
        default:         return "";
    }
}
// Stateless: evaluate each time against current fd; still zero-alloc.
static inline int clog_color_enabled_(void) {
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color) return 0;
#        if CLOG_COLOR_FORCE
    return 1;
#        else
#            if defined(CLOG_IMPLEMENTATION) && !defined(_WIN32)
    /* POSIX isatty() is cheap */
#            endif
    int curfd = clog_fd_load_();
#            if defined(CLOG_IMPLEMENTATION)
    return clog_isatty_fd_(curfd) ? 1 : 0;
#            else
    (void)curfd;
    return 0;
#            endif
#        endif
}
#    else
static inline const char *clog_level_color_(clog_level l) {
    (void)l;
    return "";
}
static inline int clog_color_enabled_(void) { return 0; }
#        define CLOG_ANSI_RESET ""
#    endif

// helpers
static inline const char *clog_level_name_(clog_level l) {
    switch (l) {
        case CLOG_TRACE: return "TRACE";
        case CLOG_DEBUG: return "DEBUG";
        case CLOG_INFO:  return "INFO";
        case CLOG_WARN:  return "WARN";
        case CLOG_ERROR: return "ERROR";
        case CLOG_FATAL: return "FATAL";
        default:         return "?";
    }
}
static inline const char *clog_basename_(const char *p) {
    const char *s = p, *b = p;
    for (; *s; ++s)
        if (*s == '/' || *s == '\\') b = s + 1;
    return b;
}
static inline uint64_t clog_hash64_(const char *s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c; (c = (unsigned char)*s++);) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

static inline size_t clog_write_prefix_(
    char *dst, size_t cap, clog_level lvl, const char *file, int line, const char *group
) {
    int Y, m, d, H, M, S, ms;
    clog_localtime_parts_(&Y, &m, &d, &H, &M, &S, &ms);
    const char *fname = clog_basename_(file);
    size_t      pos   = 0;

    /* Helper to append formatted text into dst using a size_t cursor. */
#    define CLOG_APPEND(fmt, ...)                                                 \
        do {                                                                      \
            if (pos < cap) {                                                      \
                size_t avail = cap - pos;                                         \
                int    r     = snprintf(dst + pos, avail, fmt, __VA_ARGS__);      \
                if (r > 0) {                                                      \
                    size_t rr = (size_t)r;                                        \
                    /* Advance by what fit (avail includes space for the NUL). */ \
                    pos += (rr < avail ? rr : (avail ? avail - 1 : 0));           \
                }                                                                 \
            }                                                                     \
        } while (0)

    CLOG_APPEND("%04d-%02d-%02d %02d:%02d:%02d.%03d ", Y, m, d, H, M, S, ms);

    if (clog_color_enabled_()) {
        const char *c = clog_level_color_(lvl);
        CLOG_APPEND("[%s%s%s]\t", c, clog_level_name_(lvl), CLOG_ANSI_RESET);
    } else {
        CLOG_APPEND("[%s]\t", clog_level_name_(lvl));
    }

#    if CLOG_WITH_BUILD_IN_PREFIX
#        ifdef CLOG_BUILD
    CLOG_APPEND("[build:%s] ", CLOG_BUILD);
#        endif
#    endif

#    if CLOG_WITH_TID
    unsigned long tid = clog_tid_();
#        if CLOG_TID_SHORT
    CLOG_APPEND("(t#%06lx) ", tid & 0xFFFFFFul);
#        else
    CLOG_APPEND("(tid:%lu) ", tid);
#        endif
#    endif

#    if CLOG_WITH_LINE
    CLOG_APPEND("<%s:%d> ", fname, line);
#    else
    CLOG_APPEND("<%s> ", fname);
#    endif

    if (group && *group) { CLOG_APPEND("[%s] ", group); }

#    undef CLOG_APPEND

    if (pos >= cap) return cap;
    return pos;
}

static inline int clog_write_all_(int fd, const char *p, size_t n) {
    size_t left = n;
    while (left) {
#    if defined(_WIN32)
        int r = CLOG_WRITE(fd, p, (unsigned)left);
        if (r > 0) {
            p += r;
            left -= (size_t)r;
        } else if (r < 0 && errno == EINTR) continue;
        else return -1;
#    else
        ssize_t r = CLOG_WRITE(fd, p, left);
        if (r > 0) {
            p += (size_t)r;
            left -= (size_t)r;
        } else if (r < 0 && errno == EINTR) continue;
        else return -1;
#    endif
    }
    return 0;
}

static inline void clog_emit_(
    clog_level lvl, const char *file, int line, const char *group, const char *fmt, va_list ap
) {
    if ((int)lvl < clog_lvl_load_()) return;

    int    fd        = clog_fd_load_();
    char  *buf       = g_buf;
    size_t cap       = CLOG_LINE_MAX;
    size_t off       = clog_write_prefix_(buf, cap, lvl, file, line, group);
    bool   truncated = false;

    if (off < cap) {
        va_list ap2;
        va_copy(ap2, ap);

        size_t avail = cap - off; /* size_t, not int */
        int    n     = vsnprintf(buf + off, avail, fmt, ap2);
        va_end(ap2);

        if (n > 0) {
            size_t nn = (size_t)n;
            if (nn >= avail) {
                off       = cap - 1; /* keep last byte for '\0' */
                truncated = true;
            } else {
                off += nn;
            }
        }
    }

    size_t mlen = 3;
    if (truncated && off + mlen < cap) {
        const char *mark = "...";
        memcpy(buf + off, mark, mlen);
        off += mlen;
    }

    if (off == 0 || buf[off - 1] != '\n') {
        if (off + 1 < cap) {
            buf[off++] = '\n';
            buf[off]   = '\0';
        } else {
            buf[cap - 2] = '\n';
            buf[cap - 1] = '\0';
            off          = cap - 1;
        }
    }

#    if CLOG_THREAD_SAFE
    clog_lock_();
    (void)clog_write_all_(fd, buf, off);
    clog_unlock_();
#    else
    (void)clog_write_all_(fd, buf, off);
#    endif

#    if defined(_WIN32)
    if (lvl == CLOG_FATAL) { _commit(fd); }
#    else
    if (lvl == CLOG_FATAL) (void)fsync(fd);
#    endif
}

// public funcs
void       clog_set_level(clog_level lvl) { clog_lvl_store_((int)lvl); }
clog_level clog_get_level(void) { return (clog_level)clog_lvl_load_(); }

void clog_vlog_file_line_(clog_level lvl, const char *file, int line, const char *group, const char *fmt, va_list ap) {
    clog_emit_(lvl, file, line, group, fmt, ap);
}
void clog_log_file_line_(clog_level lvl, const char *file, int line, const char *group, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    clog_vlog_file_line_(lvl, file, line, group, fmt, ap);
    va_end(ap);
}

// timers (call-site aware)
static inline int clog_timer_find_slot_(uint64_t key) {
    for (int i = 0; i < CLOG_TIMERS_MAX; i++)
        if (g_timers[i].used && g_timers[i].key == key) return i;
    return -1;
}
static inline int clog_timer_free_slot_(void) {
    for (int i = 0; i < CLOG_TIMERS_MAX; i++)
        if (!g_timers[i].used) return i;
    return -1;
}
void clogp_timer_start_(const char *file, int line, const char *label) {
    (void)file;
    (void)line;
    uint64_t key = clog_hash64_(label);
    int      idx = clog_timer_find_slot_(key);
    if (idx < 0) idx = clog_timer_free_slot_();
    if (idx >= 0) {
        g_timers[idx].key  = key;
        g_timers[idx].t0   = clog_now_ns_mono_();
        g_timers[idx].used = true;
    } else {
        clog_log_file_line_(
            CLOG_WARN, file, line, "timer", "no free timer slots (CLOG_TIMERS_MAX=%d)", CLOG_TIMERS_MAX
        );
    }
}
void clogp_timer_end_(const char *file, int line, const char *label) {
    uint64_t key = clog_hash64_(label);
    int      idx = clog_timer_find_slot_(key);
    if (idx < 0) {
        clog_log_file_line_(CLOG_WARN, file, line, "timer", "end_time for unknown label: %s", label);
        return;
    }
    uint64_t dt_ns     = clog_now_ns_mono_() - g_timers[idx].t0;
    g_timers[idx].used = false;
    if (dt_ns < CLOG_TIMER_NS_MAX) {
        clog_log_file_line_(CLOG_DEBUG, file, line, "timer", "[%llu ns]: %s", (unsigned long long)dt_ns, label);
    } else if (dt_ns < CLOG_TIMER_US_MAX) {
        double us = (double)dt_ns / 1e3;
        clog_log_file_line_(CLOG_DEBUG, file, line, "timer", "[%.3f " CLOG_TIMER_UNIT_US "]: %s", us, label);
    } else if (dt_ns < CLOG_TIMER_MS_MAX) {
        double ms = (double)dt_ns / 1e6;
        clog_log_file_line_(CLOG_DEBUG, file, line, "timer", "[%.3f ms]: %s", ms, label);
    } else {
        double s = (double)dt_ns / 1e9;
        clog_log_file_line_(CLOG_DEBUG, file, line, "timer", "[%.6f s]: %s", s, label);
    }
}

// banner
void clog_banner(void) {
#    ifdef CLOG_BUILD
    log_info_group("clog", "build: %s", CLOG_BUILD);
#    else
    log_info_group("clog", "logger ready");
#    endif
}

int  clog_get_fd(void) { return clog_fd_load_(); }
void clog_set_fd(int fd) {
    clog_fd_store_(fd);
    /* no cached color state; detection follows current fd per call */
}

#endif  // CLOG_IMPLEMENTATION

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CLOG_H
