#ifndef _NET_SETUP_H_
#define _NET_SETUP_H_

#include <stdbool.h>

/* Blocks until everything HTTPS needs is in place (currently: a trustworthy
 * wall-clock time from SNTP). Call after wifi_join() returns true. Idempotent.
 * Returns false if the device never became ready. */
bool net_setup_wait_ready(void);

/* True once net_setup_wait_ready() has succeeded. */
bool net_is_ready(void);

/* Short timezone name the clock was set to, or NULL if never synced. */
const char *net_timezone(void);

/* Runs fn(arg) on a task with a TLS-sized stack and blocks until it returns.
 * Returns false only if the task/semaphore could not be created. */
bool net_run_tls_task(void (*fn)(void *), void *arg);

/* Diagnostic: HTTPS GET against a known-good host, logs the status code.
 * Pass NULL for the default (https://www.google.com/generate_204). */
bool net_tls_selftest(const char *url);

#endif
