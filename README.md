# C Log — tiny, no‑alloc, (optionally) thread‑safe C logger

> Single‑header logger with colors, file:line, groups & timers.  
> Include the header everywhere; in **exactly one** `.c` file `#define CLOG_IMPLEMENTATION` before including.

- Repo: https://github.com/milchinskiy/c-log
- License: MIT
- Languages: C (C99+), C++ compatible
- Platforms: Linux, macOS, Windows

---

## Table of contents

- [Why C Log?](#why-c-log)
- [Quick start](#quick-start)
- [Public API](#public-api)
- [Log macros & levels](#log-macros--levels)
- [Groups](#groups)
- [Timers](#timers)
- [Thread safety & locking](#thread-safety--locking)
- [Colors](#colors)
- [Runtime controls](#runtime-controls)
- [Compile‑time options](#compile-time-options)
  - [Feature toggles](#feature-toggles)
  - [Levels: runtime vs compile‑time](#levels-runtime-vs-compile-time)
  - [Locking choices](#locking-choices)
  - [Formatting/prefix options](#formattingprefix-options)
  - [Timer behavior](#timer-behavior)
  - [Platform & portability](#platform--portability)
- [Redirecting to a file descriptor](#redirecting-to-a-file-descriptor)
- [Typical outputs](#typical-outputs)
- [Build notes & integration](#build-notes--integration)
- [FAQ](#faq)
- [License](#license)

---

## Why C Log?

- **Single header** — drop `c-log.h` into your tree.
- **Zero heap allocations** — fixed per‑thread buffer.
- **Thread‑safe (opt‑in)** — fast SRWLOCK/pthread mutex by default; spinlock optional.
- **Colorful, readable prefixes** with timestamp, level, file:line, thread id, optional group & build tag.
- **Timers** — nanosecond, microsecond, millisecond, or second units; convenient scope helper.
- **No external deps** beyond the standard C runtime and pthreads on POSIX (see notes).

---

## Quick start

```c
// main.c
#define CLOG_IMPLEMENTATION
#include "c-log.h"

int main(void) {
    clog_banner();  // "logger ready" or "build: ..."

    log_info("Hello, %s!", "world");
    log_warn_group("net", "retrying in %d ms", 200);

    clog_set_level(CLOG_DEBUG);  // show DEBUG and above

    CLOG_SCOPE_TIME("demo work") {
        // ... do some work ...
        log_debug("working...");
    }

    log_error("oops: %d", 42);
    return 0;
}
```

**Build (POSIX):**
```bash
cc -std=c11 -O2 main.c -lpthread -o demo
```

**Build (Windows, MSVC):**
```bat
cl /O2 /std:c11 main.c
```

> On Windows, the logger enables ANSI coloring for the console automatically when possible.

---

## Public API

```c
void       clog_set_level(clog_level lvl);
clog_level clog_get_level(void);

int  clog_get_fd(void);
void clog_set_fd(int fd);

void clog_banner(void);

// Timers (call‑site aware; prefer macros below):
void clogp_timer_start_(const char *file, int line, const char *label);
void clogp_timer_end_(const char *file, int line, const char *label);
```

### Types
```c
typedef enum { CLOG_TRACE = 0, CLOG_DEBUG, CLOG_INFO, CLOG_WARN, CLOG_ERROR, CLOG_FATAL } clog_level;
```

> **Note**: The `clogp_*` timer functions are public for completeness, but you’ll usually call the convenience macros shown in [Timers](#timers).

---

## Log macros & levels

Each level has two forms: plain and **grouped** (`*_group(group, ...)`).

```c
log_trace("...");          log_trace_group("subsys", "...");
log_debug("...");          log_debug_group("db", "...");
log_info("...");           log_info_group("clog", "...");
log_warn("...");           log_warn_group("net", "...");
log_error("...");          log_error_group("io", "...");
log_fatal("...");          log_fatal_group("core", "...");
```

- **Runtime threshold** controls what is *emitted* (see `clog_set_level`).
- **Compile‑time minimum** controls what is *compiled in* (see `CLOG_COMPILETIME_MIN_LEVEL`).  
  Below the compile‑time minimum, macros become `((void)0)` and carry zero cost.

---

## Groups

Groups let you tag a log line with a sub‑system label that appears in the prefix:

```text
2025-01-01 12:34:56.789 [INFO] [main.c:12] (tid:1234) [net] starting client
```

Use the `*_group("net", "...")` variants to set the group string.

---

## Timers

Timers are **call‑site aware** and require **no allocations**. You can time a labeled section using either explicit `start/end` or the scope helper.

### Explicit
```c
clog_start_time("load assets");
// ... work ...
clog_end_time("load assets");  // emits at DEBUG level
```

### Scope helper
```c
CLOG_SCOPE_TIME("parse file") {
    // ... work ...
}
```

Timer output unit is chosen automatically by duration:

- `< CLOG_TIMER_NS_MAX` → **ns**
- `< CLOG_TIMER_US_MAX` → **µs** (or custom unit string)
- `< CLOG_TIMER_MS_MAX` → **ms**
- otherwise → **s**

> Timer logs are emitted at **DEBUG** level. Ensure your runtime/compile‑time level allows `CLOG_DEBUG`.

**Capacity:** Each thread has `CLOG_TIMERS_MAX` slots. If you exceed it, a warning is logged.

---

## Thread safety & locking

- Per‑thread **scratch buffer** (`CLOG_LINE_MAX` bytes) and **timer slots** (`CLOG_TIMERS_MAX`) use `CLOG_THREADLOCAL` storage.
- Emission is protected by a global lock when `CLOG_THREAD_SAFE=1`.

**Lock kinds** (see also [Locking choices](#locking-choices)):

1. `CLOG_LOCK_KIND=1` — **Spinlock** using `atomic_flag` with bounded spins (`CLOG_SPIN_ITERS`), yielding periodically.
2. `CLOG_LOCK_KIND=2` — **Mutex** (**default**): SRWLOCK on Windows, `pthread_mutex_t` on POSIX.
3. `CLOG_LOCK_KIND=0` — **No locking** (fastest, but not thread‑safe).

---

## Colors

- Colors are enabled when `CLOG_COLOR=1` **and** output is a TTY.  
  - On Windows, the logger attempts to enable *VT processing* for the console handle.
- Set `CLOG_COLOR_FORCE=1` to colorize **even when not a TTY** (useful for tools that capture ANSI).
- Respect the standard `NO_COLOR` environment variable — if set and non‑empty, colors are disabled.
- Level → color:
  - TRACE gray, DEBUG cyan, INFO green, WARN yellow, ERROR red, FATAL magenta.

---

## Runtime controls

| Control | How | Notes |
|---|---|---|
| Change current level | `clog_set_level(CLOG_DEBUG);` | Affects emission threshold. |
| Read current level | `clog_get_level();` |  |
| Redirect output | `clog_set_fd(fd);` | Pass a **file descriptor** (not `FILE*`). Color detection follows the current fd per call. |
| Banner | `clog_banner();` | Emits `"logger ready"` or `"build: <CLOG_BUILD>"` if provided. |
| Colors off via env | `NO_COLOR=1 ./app` | Overrides any compile‑time default when `CLOG_COLOR=1`. |

---

## Compile‑time options

You can define these macros at compile time (e.g., `-DNAME=value`) **before** including `c-log.h`.

### Feature toggles

| Macro | Default | Meaning |
|---|---:|---|
| `CLOG_THREAD_SAFE` | `1` | Enable locking around writes (see lock kind). |
| `CLOG_LOCK_KIND` | `2` | `0` none, `1` spin, `2` mutex (SRWLOCK / pthread). |
| `CLOG_SPIN_ITERS` | `100` | Spin iterations before yielding (kind=1). |
| `CLOG_LINE_MAX` | `1024` | Per‑thread output buffer size. Lines longer than this are truncated and tagged with `"[TRUNC]"`. |
| `CLOG_TIMERS_MAX` | `16` | Timer slots per thread. |
| `CLOG_COLOR` | `1` | Enable color support (TTY‑aware). |
| `CLOG_COLOR_FORCE` | `0` | Force colors regardless of TTY. |
| `CLOG_WITH_LINE` | `1` | Include `file:line` in prefix. |
| `CLOG_WITH_TID` | `1` | Include thread id `(tid:...)`. |
| `CLOG_TID_SHORT` | `0` | If `1`, use low 24 bits as hex: `(t#XXXXXX)`. |
| `CLOG_WITH_BUILD_IN_PREFIX` | `0` | If `1` and `CLOG_BUILD` is defined, include `[build:<CLOG_BUILD>]` in every prefix. |
| `CLOG_TIME_UTC` | `0` | If `1`, timestamps are UTC; otherwise local time. |

### Levels: runtime vs compile‑time

| Macro | Default | Effect |
|---|---:|---|
| `CLOG_DEFAULT_LEVEL` | `CLOG_INFO` | Starting **runtime** threshold. Can be changed at run time via `clog_set_level`. You can override with `-DCLOG_LEVEL=CLOG_DEBUG` (shorthand). |
| `CLOG_COMPILETIME_MIN_LEVEL` | `CLOG_TRACE` | **Compile‑time** elision threshold: log macros below this level compile to no‑ops. You can set via `-DCLOG_MIN_LEVEL=CLOG_WARN`. |

> Precedence: `CLOG_DEFAULT_LEVEL` resolves to `CLOG_LEVEL` if provided; otherwise `CLOG_INFO`.  
> `CLOG_COMPILETIME_MIN_LEVEL` resolves to `CLOG_MIN_LEVEL` if provided; otherwise `CLOG_TRACE`.

### Locking choices

| `CLOG_LOCK_KIND` | Windows | POSIX | Notes |
|---:|---|---|---|
| `0` | none | none | Not thread‑safe; fastest. |
| `1` | `atomic_flag` + `SwitchToThread()` | `atomic_flag` + `sched_yield()` | Bounded spin, `CLOG_SPIN_ITERS` spins before yield. |
| `2` (default) | `SRWLOCK` | `pthread_mutex_t` | Recommended general choice. Link with `-lpthread` on POSIX. |

### Formatting/prefix options

Prefix format is:
```
YYYY-MM-DD HH:MM:SS.mmm [LEVEL] [file:line] (tid:123) [group?] [build:?] message...
```
- `file:line` can be disabled with `CLOG_WITH_LINE=0` (then just `[file]`).
- Thread id formatting can be short with `CLOG_TID_SHORT=1` → `(t#XXXXXX)`.
- A build tag may be present if both `CLOG_WITH_BUILD_IN_PREFIX=1` and `CLOG_BUILD="..."` are defined.

### Timer behavior

| Macro | Default | Effect |
|---|---:|---|
| `CLOG_TIMER_NS_MAX` | `1000ULL` | Durations `<` this emit in **ns**. |
| `CLOG_TIMER_US_MAX` | `1000000ULL` | Durations `<` this emit in **µs**. |
| `CLOG_TIMER_MS_MAX` | `1000000000ULL` | Durations `<` this emit in **ms**; otherwise **s**. |
| `CLOG_TIMER_UNIT_US` | `"µs"` | Unit string for microseconds (override with `-DCLOG_TIMER_UNIT_US="\"us\""`). |

> Timer labels are hashed (FNV‑1a 64‑bit) to identify slots. Collisions are possible but rare.

### Platform & portability

- **Windows**: uses `_write`, `GetLocalTime`/`GetSystemTime`, `QueryPerformanceCounter`, and SRWLOCK; enables VT/ANSI for the console when possible.
- **POSIX**: uses `write`, `clock_gettime(CLOCK_REALTIME | CLOCK_MONOTONIC)`, `pthread_mutex_t` (when locking), and `isatty` for color detection.
- C11/C++: thread‑local storage uses `CLOG_THREADLOCAL` (`_Thread_local` or `__declspec(thread)` depending on platform).

---

## Redirecting to a file descriptor

```c
#include <fcntl.h>
#include <unistd.h>

int fd = open("app.log", O_CREAT | O_TRUNC | O_WRONLY, 0644);
if (fd >= 0) {
    clog_set_fd(fd);
    log_info("this goes to app.log");
}
```

- Pass a **file descriptor** (not `FILE*`). If you need `FILE*`, grab its fd via `fileno(fp)`.
- On `CLOG_FATAL`, the logger **flushes** the fd (`fsync` on POSIX, `_commit` on Windows).

---

## Typical outputs

**TTY (colorized)**:
```
2025-09-05 10:15:00.123 [INFO]  [main.c:10] (tid:4242) logger ready
2025-09-05 10:15:00.125 [WARN]  [net.c:88]  (tid:4243) [net] reconnect in 200 ms
2025-09-05 10:15:00.128 [DEBUG] [load.c:55] (tid:4242) [timer] [472.331 µs]: parse file
```

**Plain (no color)**:
```
2025-09-05 10:15:00.123 [INFO] [main.c:10] (tid:4242) logger ready
```

When a message exceeds `CLOG_LINE_MAX`, it is truncated and tagged:
```
... [TRUNC]
```

---

## Build notes & integration

### Single‑header pattern
Include `c-log.h` everywhere. In **exactly one** translation unit:

```c
#define CLOG_IMPLEMENTATION
#include "c-log.h"
```

### CMake example

```cmake
# CMakeLists.txt
add_library(clog INTERFACE)
target_include_directories(clog INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Your app/library
add_executable(app main.c)
target_link_libraries(app PRIVATE clog)
target_compile_definitions(app PRIVATE CLOG_IMPLEMENTATION)
if (UNIX)
  target_link_libraries(app PRIVATE pthread)
endif()
```

### Toolchains
- **GCC/Clang**: supports `__attribute__((format(printf,...)))` for format checking.
- **MSVC**: format checking attribute is ignored (harmless).

### Notes
- On older glibc, you might need `-lrt` for `clock_gettime`. Modern toolchains don’t.
- Not async‑signal‑safe (uses `snprintf`, locks, etc.). Avoid calling from signal handlers.

---

## FAQ

**How do I disable colors in CI?**  
- Set the environment variable: `NO_COLOR=1`.  
- Or compile with `-DCLOG_COLOR=0`.  
- Or redirect output to a non‑TTY (colors auto‑disable unless `CLOG_COLOR_FORCE=1`).

**Why doesn’t my timer print?**  
- Timers emit at `CLOG_DEBUG`. Ensure both `clog_set_level(CLOG_DEBUG)` (runtime) **and** `CLOG_COMPILETIME_MIN_LEVEL <= CLOG_DEBUG` (compile‑time).

**Can I log to stdout instead of stderr?**  
- Yes: `clog_set_fd(1);` (stdout). Default is `2` (stderr).

**What happens on partial writes or EINTR?**  
- The logger retries until all bytes are written (handles `EINTR`).

**Is it safe to use in multiple threads?**  
- Yes, when `CLOG_THREAD_SAFE=1` (default). Set `CLOG_LOCK_KIND` per your needs.

**Can I add my build id to every line?**  
- Define `-DCLOG_WITH_BUILD_IN_PREFIX=1 -DCLOG_BUILD="\"v1.2.3\""`.

---

## License

MIT — see [LICENSE](LICENSE).

---

## Appendix: Option reference (cheat sheet)

```text
Levels
  Runtime:    clog_set_level(CLOG_TRACE|DEBUG|INFO|WARN|ERROR|FATAL)
  Compile:    -DCLOG_MIN_LEVEL=CLOG_WARN   (elide below WARN)
  Default:    -DCLOG_LEVEL=CLOG_DEBUG      (runtime default)

Colors
  Enable:     -DCLOG_COLOR=1               (default)
  Force:      -DCLOG_COLOR_FORCE=1
  Disable:    -DCLOG_COLOR=0  or NO_COLOR=1

Prefix
  File:line:  -DCLOG_WITH_LINE=1           (default)
  TID:        -DCLOG_WITH_TID=1            (default), -DCLOG_TID_SHORT=1 for hex
  UTC:        -DCLOG_TIME_UTC=1
  Build tag:  -DCLOG_WITH_BUILD_IN_PREFIX=1 -DCLOG_BUILD="\"hash\""

Locking
  Thread‑safe: -DCLOG_THREAD_SAFE=1        (default)
  Kind:        -DCLOG_LOCK_KIND=2|1|0
  Spin iters:  -DCLOG_SPIN_ITERS=100

Buffers & timers
  Line max:    -DCLOG_LINE_MAX=1024
  Timers max:  -DCLOG_TIMERS_MAX=16
  Units:       -DCLOG_TIMER_UNIT_US="\"us\""
  Thresholds:  -DCLOG_TIMER_NS_MAX=1000 -DCLOG_TIMER_US_MAX=1000000 -DCLOG_TIMER_MS_MAX=1000000000
```
