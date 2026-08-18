// Wraps the vendored my_basic_src.inc (renamed from upstream's my_basic.c
// so PlatformIO's library dependency finder doesn't also try to compile
// it directly -- it's #included here instead) with pragmas silencing
// upstream's own build warnings, which this project's build_flags
// escalate to hard errors. Not our code to fix; see the two real (non-
// pragma-able) issues this project's own patch already handles inside
// my_basic_src.inc itself: `_lock_t` renamed to `_mb_lock_t` throughout
// (a purely internal type, absent from my_basic.h's public API) because
// it collided with ESP-IDF newlib's own `_lock_t` typedef in
// components/newlib/platform_include/sys/lock.h, pulled in transitively
// via <assert.h>.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Woverflow"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "my_basic_src.inc"
#pragma GCC diagnostic pop
