/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_PERSISTENT_LOG_H
#define B2L_PERSISTENT_LOG_H
#include "b2l_log_core.h"
/* Append is safe before VM/scheduler initialization. It does not access disk. */
void b2l_log_append(char c);
void b2l_log_append_bytes(const void*,size_t);
/* Call once after native UFS is ready, outside its mutex. No formatting occurs. */
int b2l_log_start(const struct b2l_log_sink *sink);
/* B1L-0220: write-ahead trace of kernel entries from com.apple.WebKit.* processes (b2l_persistent_log.c). */
void b2l_wk_trace(const char *what, uint64_t a, uint64_t b, uint64_t c);
#endif
