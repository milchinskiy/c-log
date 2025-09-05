#include "c-log.h"

int main(void) {
    clog_set_level(CLOG_TRACE);
    clog_set_fd(fileno(stderr));

    log_info("demo starting");
    log_warn_group("startup", "low entropy seed; continuing anyway");

    CLOG_SCOPE_TIME("pretend work") {
#if defined(_WIN32)
        Sleep(50);
#else
        struct timespec ts = {0, 50 * 1000 * 1000};  // 50ms
        nanosleep(&ts, NULL);
#endif
    }

    CLOG_SCOPE_TIME("just a test") {
        log_trace("trace test");
        log_debug("debug test");
        log_info("info test");
        log_warn("warn test");
        log_error("error test");
        log_fatal("fatal test");
    }

    log_error("something went %s", "sideways");
    return 0;
}
