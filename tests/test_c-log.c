#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>
    #include <sys/stat.h>
    #include <windows.h>
    #define PIPE         _pipe
    #define DUP          _dup
    #define DUP2         _dup2
    #define CLOSE        _close
    #define READ         _read
    #define O_PIPE_FLAGS (_O_BINARY | _O_NOINHERIT)
static void sleep_ms_(int ms) { Sleep(ms); }
static void set_no_color_(void) { _putenv("NO_COLOR=1"); }
#else
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <time.h>
    #include <unistd.h>
    #define PIPE         pipe
    #define DUP          dup
    #define DUP2         dup2
    #define CLOSE        close
    #define READ         read
    #define O_PIPE_FLAGS 0
static void sleep_ms_(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
static void set_no_color_(void) { setenv("NO_COLOR", "1", 1); }
#endif

#include "c-log.h"  // interface only; impl compiled in src/c-log-impl.c

// ------- tiny capture of stderr to memory (no files) -------
typedef struct {
    int saved_stderr;
    int rfd;
} cap_t;

static int cap_begin(cap_t* cap) {
    int fds[2];
#if defined(_WIN32)
    if (PIPE(fds, 64 * 1024, O_PIPE_FLAGS) != 0) return 1;
#else
    if (PIPE(fds) != 0) return 1;
#endif
    cap->saved_stderr = DUP(2);
    if (cap->saved_stderr < 0) {
        CLOSE(fds[0]);
        CLOSE(fds[1]);
        return 2;
    }
    if (DUP2(fds[1], 2) < 0) {
        CLOSE(fds[0]);
        CLOSE(fds[1]);
        CLOSE(cap->saved_stderr);
        return 3;
    }
    CLOSE(fds[1]);      // fd 2 now refers to the pipe's write end
    cap->rfd = fds[0];  // we'll read from this after restore
    return 0;
}

static char* cap_end(cap_t* cap, size_t* out_len) {
    // Restore stderr; this also closes the pipe write end (fd 2),
    // so the reader below will see EOF when all data is drained.
    if (cap->saved_stderr >= 0) {
        DUP2(cap->saved_stderr, 2);
        CLOSE(cap->saved_stderr);
    }
    // Drain the pipe
    size_t capsz = 8192, len = 0;
    char*  buf = (char*)malloc(capsz);
    if (!buf) {
        CLOSE(cap->rfd);
        return NULL;
    }
    for (;;) {
        if (len + 4096 > capsz) {
            size_t newsz = capsz * 2;
            char*  nb    = (char*)realloc(buf, newsz);
            if (!nb) {
                free(buf);
                CLOSE(cap->rfd);
                return NULL;
            }
            buf   = nb;
            capsz = newsz;
        }
        ssize_t r = READ(cap->rfd, buf + len, 4096);
        if (r > 0) {
            len += (size_t)r;
            continue;
        }
        break;  // r == 0 (EOF) or <0 (unlikely here)
    }
    CLOSE(cap->rfd);
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

static int count_substr(const char* s, const char* needle) {
    int    c = 0;
    size_t n = strlen(needle);
    if (!n) return 0;
    for (const char* p = s; (p = strstr(p, needle)); p += n) ++c;
    return c;
}

static int contains(const char* hay, const char* needle) { return strstr(hay, needle) != NULL; }

static int count_char(const char* s, char c) {
    int k = 0;
    for (; *s; ++s)
        if (*s == c) ++k;
    return k;
}

// -------- tests --------

static int test_level_and_basic_prefix(void) {
    set_no_color_();
    cap_t cap;
    if (cap_begin(&cap) != 0) return 10;

    clog_set_level(CLOG_ERROR);
    log_info("hello info (should NOT appear)");
    log_error("boom");

    size_t n   = 0;
    char*  out = cap_end(&cap, &n);
    if (!out) return 11;

    int ok = contains(out, "[ERROR]") && contains(out, "boom") && !contains(out, "hello info");
    free(out);
    return ok ? 0 : 12;
}

static int test_group_and_fileline(void) {
    set_no_color_();
    cap_t cap;
    if (cap_begin(&cap) != 0) return 20;

    clog_set_level(CLOG_TRACE);
    log_error_group("group name", "msg %s", "with formatting");

    size_t n   = 0;
    char*  out = cap_end(&cap, &n);
    if (!out) return 21;

    // Expect: [ERROR], [group name], and this source file's base name
    int ok = contains(out, "[ERROR]") && contains(out, "[group name]") &&
             (contains(out, "<test_c-log.c:") || contains(out, "<test_c-log.c>"));
    free(out);
    return ok ? 0 : 22;
}

static int test_timer_line_and_callsite(void) {
    set_no_color_();
    cap_t cap;
    if (cap_begin(&cap) != 0) return 30;

    clog_set_level(CLOG_DEBUG);
    clog_start_time("some label");
    sleep_ms_(5);
    clog_end_time("some label");

    size_t n   = 0;
    char*  out = cap_end(&cap, &n);
    if (!out) return 31;

    int ok = contains(out, "[DEBUG]") && contains(out, "timer") && contains(out, "some label") &&
             (contains(out, " ns]:") || contains(out, " µs]:") || contains(out, " us]:") || contains(out, " ms]:") ||
              contains(out, " s]:")) &&
             (contains(out, "<test_c-log.c:") || contains(out, "<test_c-log.c>"));
    free(out);
    return ok ? 0 : 32;
}

static int test_newline_integrity(void) {
    set_no_color_();
    cap_t cap;
    if (cap_begin(&cap) != 0) return 40;

    clog_set_level(CLOG_TRACE);
    log_info("line1");
    log_info("line2");
    log_info("line3");

    size_t n   = 0;
    char*  out = cap_end(&cap, &n);
    if (!out) return 41;

    /* Expectations:
       - exactly 3 INFO records
       - exactly 3 newline terminators
       - last byte of the captured stream is '\n'
       - payloads present
    */
    int infos = count_substr(out, "[INFO]");
    int nl    = count_char(out, '\n');

    int ok    = (n > 0) && (infos == 3) && (nl == 3) && (out[n - 1] == '\n') && contains(out, "line1") &&
             contains(out, "line2") && contains(out, "line3");

    free(out);
    return ok ? 0 : 42;
}

#if !defined(_WIN32) && CLOG_THREAD_SAFE
    #include <pthread.h>
typedef struct {
    int id;
    int lines;
} thr_arg_t;

static void* spammer(void* a) {
    thr_arg_t* t = (thr_arg_t*)a;
    for (int i = 0; i < t->lines; i++) { log_info_group("thr", "T%d-%d", t->id, i); }
    return NULL;
}

static int test_thread_safety_lines_not_split(void) {
    set_no_color_();
    cap_t cap;
    if (cap_begin(&cap) != 0) return 50;

    clog_set_level(CLOG_INFO);
    enum { T = 4, N = 200 };
    pthread_t th[T];
    thr_arg_t args[T];
    for (int i = 0; i < T; i++) {
        args[i].id    = i;
        args[i].lines = N;
        pthread_create(&th[i], NULL, spammer, &args[i]);
    }
    for (int i = 0; i < T; i++) { pthread_join(th[i], NULL); }

    size_t n   = 0;
    char*  out = cap_end(&cap, &n);
    if (!out) return 51;

    // We expect exactly T*N newline-terminated records (no partial splits).
    int ok = count_char(out, '\n') == T * N;
    free(out);
    return ok ? 0 : 52;
}
#endif

int main(void) {
    int rc = 0;
    rc |= test_level_and_basic_prefix();
    rc |= test_group_and_fileline();
    rc |= test_timer_line_and_callsite();
    rc |= test_newline_integrity();
#if !defined(_WIN32) && CLOG_THREAD_SAFE
    rc |= test_thread_safety_lines_not_split();
#endif

    if (rc) {
        fprintf(stderr, "Test failures (bitwise OR code): %d\n", rc);
        return 1;
    } else {
        fprintf(stdout, "All tests passed\n");
    }
    return 0;
}
