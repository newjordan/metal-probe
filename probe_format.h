#ifndef PROBE_FORMAT_H
#define PROBE_FORMAT_H

#include <stdio.h>

/* Schema version emitted by the text and JSON formatters.
 *
 * Text is six LF-terminated "key: value" lines, in this order:
 *   schema_version, device_name, architecture, kernel_count,
 *   archive_path, archive_bytes
 * architecture is the bare token null when it is unknown or empty.
 * Other strings are literal UTF-8. Backslash and the controls \b, \f,
 * \n, \r, and \t are escaped; other bytes below 0x20 use \u00XX.
 * Text does not escape quotes. JSON does.
 *
 * JSON is one object with a trailing newline, 2-space indent, and keys
 * in this order:
 *   schema_version,
 *   device { name, architecture },
 *   library { kernel_count },
 *   archive { path, bytes }
 * Strings are JSON-escaped. architecture is JSON null when unknown or empty.
 */
#define PROBE_SCHEMA_VERSION 1

typedef struct ProbeReport {
    const char *device_name;          /* required, non-NULL */
    const char *architecture;         /* NULL or "" when unknown */
    unsigned long kernel_count;
    const char *archive_path;         /* required, non-NULL */
    unsigned long long archive_bytes;
} ProbeReport;

/* Malloc'd text, including the trailing newline. Caller frees.
 * Returns NULL when report is NULL or a required string is NULL. */
char *probe_format_text(const ProbeReport *report);
char *probe_format_json(const ProbeReport *report);

/* Write the same bytes to out. Returns 0 on success, -1 on error. */
int probe_write_text(FILE *out, const ProbeReport *report);
int probe_write_json(FILE *out, const ProbeReport *report);

#endif
