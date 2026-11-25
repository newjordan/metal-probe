#include "probe_format.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;
static int g_passed = 0;

static void pass(const char *name)
{
    g_passed++;
    printf("ok %s\n", name);
}

static void fail(const char *name, const char *detail)
{
    g_failed++;
    fprintf(stderr, "FAIL %s\n%s\n", name, detail ? detail : "");
}

static void expect_eq(const char *name, const char *got, const char *want)
{
    if (got == NULL || want == NULL || strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL %s\n--- got ---\n%s\n--- want ---\n%s\n",
                name,
                got ? got : "(null)",
                want ? want : "(null)");
        g_failed++;
        return;
    }
    pass(name);
}

static void expect_null(const char *name, const char *got)
{
    if (got != NULL) {
        fprintf(stderr, "FAIL %s\nexpected NULL, got:\n%s\n", name, got);
        g_failed++;
        return;
    }
    pass(name);
}

static void expect_true(const char *name, int cond)
{
    if (!cond) {
        fail(name, "expected true");
        return;
    }
    pass(name);
}

static int count_char(const char *text, char ch)
{
    int n = 0;
    if (text == NULL) {
        return -1;
    }
    for (; *text != '\0'; text++) {
        if (*text == ch) {
            n++;
        }
    }
    return n;
}

static char *read_file(const char *path, char **error)
{
    FILE *fp;
    long n;
    char *buf;
    size_t got;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        *error = "could not open file";
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        *error = "seek failed";
        return NULL;
    }
    n = ftell(fp);
    if (n < 0) {
        fclose(fp);
        *error = "tell failed";
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        *error = "rewind failed";
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(fp);
        *error = "out of memory";
        return NULL;
    }
    got = fread(buf, 1, (size_t)n, fp);
    buf[got] = '\0';
    fclose(fp);
    if (got != (size_t)n) {
        free(buf);
        *error = "short read";
        return NULL;
    }
    return buf;
}

static const char *k_text_sample =
    "schema_version: 1\n"
    "device_name: Apple M2\n"
    "architecture: applegpu_g14g\n"
    "kernel_count: 1\n"
    "archive_path: out.metallib\n"
    "archive_bytes: 4096\n";

static const char *k_json_sample =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"device\": {\n"
    "    \"name\": \"Apple M2\",\n"
    "    \"architecture\": \"applegpu_g14g\"\n"
    "  },\n"
    "  \"library\": {\n"
    "    \"kernel_count\": 1\n"
    "  },\n"
    "  \"archive\": {\n"
    "    \"path\": \"out.metallib\",\n"
    "    \"bytes\": 4096\n"
    "  }\n"
    "}\n";

static ProbeReport sample_report(void)
{
    ProbeReport report;

    memset(&report, 0, sizeof(report));
    report.device_name = "Apple M2";
    report.architecture = "applegpu_g14g";
    report.kernel_count = 1;
    report.archive_path = "out.metallib";
    report.archive_bytes = 4096;
    return report;
}

static void test_samples(void)
{
    ProbeReport report = sample_report();
    char *text = probe_format_text(&report);
    char *json = probe_format_json(&report);

    expect_eq("sample text", text, k_text_sample);
    expect_eq("sample json", json, k_json_sample);
    expect_true("sample json newline count", count_char(json, '\n') == 14);
    free(text);
    free(json);
}

static void test_null_architecture(void)
{
    ProbeReport report = sample_report();
    char *from_null;
    char *from_empty;
    const char *text_want =
        "schema_version: 1\n"
        "device_name: Apple M2\n"
        "architecture: null\n"
        "kernel_count: 1\n"
        "archive_path: out.metallib\n"
        "archive_bytes: 4096\n";
    const char *json_want =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"device\": {\n"
        "    \"name\": \"Apple M2\",\n"
        "    \"architecture\": null\n"
        "  },\n"
        "  \"library\": {\n"
        "    \"kernel_count\": 1\n"
        "  },\n"
        "  \"archive\": {\n"
        "    \"path\": \"out.metallib\",\n"
        "    \"bytes\": 4096\n"
        "  }\n"
        "}\n";

    report.architecture = NULL;
    from_null = probe_format_json(&report);
    expect_eq("null architecture json", from_null, json_want);
    free(from_null);
    from_null = probe_format_text(&report);
    expect_eq("null architecture text", from_null, text_want);
    free(from_null);

    report.architecture = "";
    from_empty = probe_format_json(&report);
    expect_eq("empty architecture matches null json", from_empty, json_want);
    free(from_empty);
    from_empty = probe_format_text(&report);
    expect_eq("empty architecture matches null text", from_empty, text_want);
    free(from_empty);
}

static void test_escapes(void)
{
    ProbeReport report = sample_report();
    char *json;
    char *text;
    const char *json_want =
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"device\": {\n"
        "    \"name\": \"Say \\\"hi\\\"\\\\\\n\\t\\u0001\",\n"
        "    \"architecture\": \"applegpu_g14g\"\n"
        "  },\n"
        "  \"library\": {\n"
        "    \"kernel_count\": 1\n"
        "  },\n"
        "  \"archive\": {\n"
        "    \"path\": \"dir\\\\out.metallib\",\n"
        "    \"bytes\": 4096\n"
        "  }\n"
        "}\n";
    const char *text_want =
        "schema_version: 1\n"
        "device_name: Say \"hi\"\\\\\\n\\t\\u0001\n"
        "architecture: applegpu_g14g\n"
        "kernel_count: 1\n"
        "archive_path: dir\\\\out.metallib\n"
        "archive_bytes: 4096\n";

    /* Device name bytes: Say "hi" \ \n \t \x01
     * Path bytes: dir \ out.metallib */
    report.device_name = "Say \"hi\"\\\n\t\x01";
    report.archive_path = "dir\\out.metallib";
    json = probe_format_json(&report);
    text = probe_format_text(&report);
    expect_eq("json escapes", json, json_want);
    expect_eq("text escapes", text, text_want);
    expect_true("escaped json keeps structural newlines", count_char(json, '\n') == 14);
    free(json);
    free(text);
}

static void test_short_controls(void)
{
    ProbeReport report = sample_report();
    char *json;
    char *text;

    report.device_name = "\b\f\r";
    json = probe_format_json(&report);
    text = probe_format_text(&report);
    expect_true("json backspace formfeed cr",
                json != NULL && strstr(json, "\"name\": \"\\b\\f\\r\"") != NULL);
    expect_true("text backspace formfeed cr",
                text != NULL && strstr(text, "device_name: \\b\\f\\r\n") != NULL);
    free(json);
    free(text);
}

static void test_unicode_and_zero(void)
{
    ProbeReport report = sample_report();
    char *json;
    const char *name = "Metal GPU \xE2\x80\x94 test";

    report.device_name = name;
    report.kernel_count = 0;
    report.archive_bytes = 0;
    json = probe_format_json(&report);
    expect_true("unicode passthrough", json != NULL && strstr(json, name) != NULL);
    expect_true("zero kernel count", json != NULL && strstr(json, "\"kernel_count\": 0\n") != NULL);
    expect_true("zero archive bytes", json != NULL && strstr(json, "\"bytes\": 0\n") != NULL);
    free(json);
}

static void test_wide_integers(void)
{
    ProbeReport report = sample_report();
    char *json;
    char *text;

    report.kernel_count = 4294967295UL;
    report.archive_bytes = 4294967296ULL;
    json = probe_format_json(&report);
    text = probe_format_text(&report);
    expect_true("kernel count above 32-bit signed",
                json != NULL && strstr(json, "\"kernel_count\": 4294967295\n") != NULL);
    expect_true("archive bytes at 2^32 json",
                json != NULL && strstr(json, "\"bytes\": 4294967296\n") != NULL);
    expect_true("archive bytes at 2^32 text",
                text != NULL && strstr(text, "archive_bytes: 4294967296\n") != NULL);
    free(json);
    free(text);

    report.archive_bytes = ULLONG_MAX;
    json = probe_format_json(&report);
    expect_true("ullong max",
                json != NULL && strstr(json, "\"bytes\": 18446744073709551615\n") != NULL);
    free(json);
}

static void test_invalid(void)
{
    ProbeReport report = sample_report();

    expect_null("null report text", probe_format_text(NULL));
    expect_null("null report json", probe_format_json(NULL));
    expect_true("null file text", probe_write_text(NULL, &report) == -1);
    expect_true("null file json", probe_write_json(NULL, &report) == -1);

    report.device_name = NULL;
    expect_null("null device name", probe_format_json(&report));
    report = sample_report();
    report.archive_path = NULL;
    expect_null("null archive path", probe_format_text(&report));
    expect_true("null file and report", probe_write_json(NULL, NULL) == -1);
}

static char *capture(int (*write_fn)(FILE *, const ProbeReport *), const ProbeReport *report)
{
    FILE *fp;
    long n;
    char *buf;
    size_t got;

    fp = tmpfile();
    if (fp == NULL) {
        return NULL;
    }
    if (write_fn(fp, report) != 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    n = ftell(fp);
    if (n < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    got = fread(buf, 1, (size_t)n, fp);
    buf[got] = '\0';
    fclose(fp);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    return buf;
}

static void test_file_writer(void)
{
    ProbeReport report = sample_report();
    char *text = capture(probe_write_text, &report);
    char *json = capture(probe_write_json, &report);

    expect_eq("file writer text", text, k_text_sample);
    expect_eq("file writer json", json, k_json_sample);
    free(text);
    free(json);
}

static void test_readme(const char *path)
{
    char *error = NULL;
    char *readme = read_file(path, &error);

    if (readme == NULL) {
        fprintf(stderr, "FAIL readme (%s: %s)\n", path, error ? error : "unknown");
        g_failed++;
        return;
    }
    expect_true("readme contains text sample", strstr(readme, k_text_sample) != NULL);
    expect_true("readme contains json sample", strstr(readme, k_json_sample) != NULL);
    free(readme);
}

int main(int argc, char **argv)
{
    const char *readme = "README.md";

    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        readme = argv[1];
    }

    test_samples();
    test_null_architecture();
    test_escapes();
    test_short_controls();
    test_unicode_and_zero();
    test_wide_integers();
    test_invalid();
    test_file_writer();
    test_readme(readme);

    printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
