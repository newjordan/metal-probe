#include "probe_format.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Buf {
    char *data;
    size_t len;
    size_t cap;
    int error;
} Buf;

static int buf_reserve(Buf *buf, size_t extra)
{
    size_t need;
    size_t cap;
    char *next;

    if (buf->error) {
        return -1;
    }
    /* Leave one byte for the trailing NUL. */
    if (buf->len > SIZE_MAX - 1 || extra > (SIZE_MAX - 1) - buf->len) {
        buf->error = 1;
        return -1;
    }
    need = buf->len + extra + 1;
    if (need <= buf->cap) {
        return 0;
    }
    cap = buf->cap ? buf->cap : 64;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    next = realloc(buf->data, cap);
    if (next == NULL) {
        buf->error = 1;
        return -1;
    }
    buf->data = next;
    buf->cap = cap;
    return 0;
}

static void buf_putc(Buf *buf, char ch)
{
    if (buf_reserve(buf, 1) != 0) {
        return;
    }
    buf->data[buf->len++] = ch;
    buf->data[buf->len] = '\0';
}

static void buf_puts(Buf *buf, const char *text)
{
    size_t n = strlen(text);
    if (buf_reserve(buf, n) != 0) {
        return;
    }
    memcpy(buf->data + buf->len, text, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
}

static void buf_printf(Buf *buf, const char *fmt, ...)
{
    va_list ap;
    va_list copy;
    int n;

    va_start(ap, fmt);
    va_copy(copy, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        buf->error = 1;
        va_end(copy);
        return;
    }
    if (buf_reserve(buf, (size_t)n) != 0) {
        va_end(copy);
        return;
    }
    vsnprintf(buf->data + buf->len, buf->cap - buf->len, fmt, copy);
    va_end(copy);
    buf->len += (size_t)n;
}

static void buf_escape(Buf *buf, const char *text, int json)
{
    const unsigned char *p;

    for (p = (const unsigned char *)text; *p != '\0'; p++) {
        unsigned char ch = *p;
        switch (ch) {
        case '"':
            if (json) {
                buf_puts(buf, "\\\"");
            } else {
                buf_putc(buf, '"');
            }
            break;
        case '\\':
            buf_puts(buf, "\\\\");
            break;
        case '\b':
            buf_puts(buf, "\\b");
            break;
        case '\f':
            buf_puts(buf, "\\f");
            break;
        case '\n':
            buf_puts(buf, "\\n");
            break;
        case '\r':
            buf_puts(buf, "\\r");
            break;
        case '\t':
            buf_puts(buf, "\\t");
            break;
        default:
            if (ch < 0x20) {
                buf_printf(buf, "\\u%04x", (unsigned int)ch);
            } else {
                buf_putc(buf, (char)ch);
            }
            break;
        }
    }
}

static void buf_json_string(Buf *buf, const char *text)
{
    buf_putc(buf, '"');
    buf_escape(buf, text, 1);
    buf_putc(buf, '"');
}

static int report_ok(const ProbeReport *report)
{
    return report != NULL && report->device_name != NULL && report->archive_path != NULL;
}

static const char *architecture_or_null(const ProbeReport *report)
{
    if (report->architecture == NULL || report->architecture[0] == '\0') {
        return NULL;
    }
    return report->architecture;
}

static char *finish(Buf *buf)
{
    if (buf->error || buf->data == NULL) {
        free(buf->data);
        return NULL;
    }
    return buf->data;
}

char *probe_format_text(const ProbeReport *report)
{
    Buf buf;
    const char *architecture;

    if (!report_ok(report)) {
        return NULL;
    }
    memset(&buf, 0, sizeof(buf));
    architecture = architecture_or_null(report);

    buf_printf(&buf, "schema_version: %d\n", PROBE_SCHEMA_VERSION);
    buf_puts(&buf, "device_name: ");
    buf_escape(&buf, report->device_name, 0);
    buf_putc(&buf, '\n');
    buf_puts(&buf, "architecture: ");
    if (architecture == NULL) {
        buf_puts(&buf, "null");
    } else {
        buf_escape(&buf, architecture, 0);
    }
    buf_putc(&buf, '\n');
    buf_printf(&buf, "kernel_count: %lu\n", report->kernel_count);
    buf_puts(&buf, "archive_path: ");
    buf_escape(&buf, report->archive_path, 0);
    buf_putc(&buf, '\n');
    buf_printf(&buf, "archive_bytes: %llu\n", report->archive_bytes);
    return finish(&buf);
}

char *probe_format_json(const ProbeReport *report)
{
    Buf buf;
    const char *architecture;

    if (!report_ok(report)) {
        return NULL;
    }
    memset(&buf, 0, sizeof(buf));
    architecture = architecture_or_null(report);

    buf_puts(&buf, "{\n");
    buf_printf(&buf, "  \"schema_version\": %d,\n", PROBE_SCHEMA_VERSION);
    buf_puts(&buf, "  \"device\": {\n");
    buf_puts(&buf, "    \"name\": ");
    buf_json_string(&buf, report->device_name);
    buf_puts(&buf, ",\n");
    buf_puts(&buf, "    \"architecture\": ");
    if (architecture == NULL) {
        buf_puts(&buf, "null");
    } else {
        buf_json_string(&buf, architecture);
    }
    buf_puts(&buf, "\n");
    buf_puts(&buf, "  },\n");
    buf_puts(&buf, "  \"library\": {\n");
    buf_printf(&buf, "    \"kernel_count\": %lu\n", report->kernel_count);
    buf_puts(&buf, "  },\n");
    buf_puts(&buf, "  \"archive\": {\n");
    buf_puts(&buf, "    \"path\": ");
    buf_json_string(&buf, report->archive_path);
    buf_puts(&buf, ",\n");
    buf_printf(&buf, "    \"bytes\": %llu\n", report->archive_bytes);
    buf_puts(&buf, "  }\n");
    buf_puts(&buf, "}\n");
    return finish(&buf);
}

static int write_formatted(FILE *out, char *text)
{
    int rc;

    if (text == NULL || out == NULL) {
        free(text);
        return -1;
    }
    rc = fputs(text, out) == EOF ? -1 : 0;
    free(text);
    return rc;
}

int probe_write_text(FILE *out, const ProbeReport *report)
{
    return write_formatted(out, probe_format_text(report));
}

int probe_write_json(FILE *out, const ProbeReport *report)
{
    return write_formatted(out, probe_format_json(report));
}
