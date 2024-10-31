/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2024 Enrico Weigelt, metux IT consult <info@metux.net>
 */
#ifndef __XORG_OS_LOGGING_H
#define __XORG_OS_LOGGING_H

#include "include/os.h"

/**
 * @brief initialize logging and open log files
 *
 * Make backup of existing log file, create a new one and open it for logging.
 *
 * - May be called with NULL or "", if no logging is desired.
 *
 * - Must always be called, otherwise log messages will fill up the buffer and
 *   let it grow infinitely.
 *
 * - if "%s" is present in fname, it will be replaced with the display string or pid
 *
 * @param fname log file name template. if NULL, dont write any log.
 * @param backup name for the old logfile.
 * @return new log file name
 */
const char* LogInit(const char *fname, const char *backup);

/**
 * @brief rename the current log file according display name
 *
 * Renames the current log file with per display prefix (e.g. "Xorg.log.0")
 *
 */
void LogSetDisplay(void);

/**
 * @brief log exit code, then flush and close log file and write
 *
 * Logs the exit code (and success/error state), then flush and close log file.
 */
void LogClose(enum ExitCode error);

#ifdef DEBUG
/**
 * @brief log debug messages (like errors) if symbol DEBUG is defined
 */
#define DebugF ErrorF
#else
#define DebugF(...)             /* */
#endif

/**
 * @brief console log verbosity (stderr)
 *
 * The verbosity level of logging to console. All messages with verbosity
 * level below this one will be written to stderr
 */
extern int xorgLogVerbosity;

/**
 * @brief log file verbosity
 *
 * The verbosity level of logging to per-display file. All messages with
 * verbosity level below this one will be written to the log file.
 */
extern int xorgLogFileVerbosity;

/**
 * @brief force fsync() on each log write
 *
 * If set to TRUE, force fsync() on each log write.
 */
extern Bool xorgLogSync;

#endif /* __XORG_OS_LOGGING_H */
