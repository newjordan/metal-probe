#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "probe_format.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *fp, const char *prog)
{
    fprintf(fp,
            "usage: %s [--json] <output.metallib>\n"
            "\n"
            "Report the default Metal device and write a one-kernel binary archive.\n"
            "\n"
            "  --json    print the report as JSON schema version %d\n"
            "  -h, --help  show this help\n"
            "\n"
            "Without --json the report is stable line-oriented text. Diagnostics go\n"
            "to stderr. A successful --json run writes only the JSON document to stdout.\n",
            prog,
            PROBE_SCHEMA_VERSION);
}

static const char *error_detail(NSError *error)
{
    const char *detail = NULL;
    if (error != nil && error.localizedDescription != nil) {
        detail = error.localizedDescription.UTF8String;
    }
    return detail != NULL ? detail : "unknown error";
}

static int fail_error(const char *prog, const char *what, NSError *error)
{
    fprintf(stderr, "%s: %s: %s\n", prog, what, error_detail(error));
    return 1;
}

static int format_report(const char *prog, int json, const ProbeReport *report)
{
    int rc = json ? probe_write_json(stdout, report) : probe_write_text(stdout, report);
    if (rc != 0) {
        fprintf(stderr, "%s: could not format report\n", prog);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const char *prog = (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') ? argv[0] : "probe";
        const char *out_path = NULL;
        int json = 0;
        int end_of_flags = 0;
        int i;

        for (i = 1; i < argc; i++) {
            const char *arg = argv[i];
            if (!end_of_flags && strcmp(arg, "--") == 0) {
                end_of_flags = 1;
                continue;
            }
            if (!end_of_flags && (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)) {
                usage(stdout, prog);
                return 0;
            }
            if (!end_of_flags && strcmp(arg, "--json") == 0) {
                json = 1;
                continue;
            }
            if (!end_of_flags && arg[0] == '-') {
                fprintf(stderr, "%s: unknown option: %s\n", prog, arg);
                usage(stderr, prog);
                return 1;
            }
            if (out_path != NULL) {
                fprintf(stderr, "%s: unexpected argument: %s\n", prog, arg);
                usage(stderr, prog);
                return 1;
            }
            if (arg[0] == '\0') {
                fprintf(stderr, "%s: output path is empty\n", prog);
                usage(stderr, prog);
                return 1;
            }
            out_path = arg;
        }

        if (out_path == NULL) {
            usage(stderr, prog);
            return 1;
        }

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            fprintf(stderr, "%s: no Metal device\n", prog);
            return 1;
        }

        const char *device_name = device.name.UTF8String;
        if (device_name == NULL) {
            fprintf(stderr, "%s: Metal device has no name\n", prog);
            return 1;
        }

        const char *architecture = NULL;
        if (@available(macOS 14.0, *)) {
            MTLArchitecture *gpu_arch = device.architecture;
            if (gpu_arch != nil && gpu_arch.name != nil) {
                architecture = gpu_arch.name.UTF8String;
            }
        }

        NSString *source =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "kernel void k(device float* o [[buffer(0)]], uint gid [[thread_position_in_grid]]) { o[gid] = 1.0f; }\n";
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source
                                                      options:[MTLCompileOptions new]
                                                        error:&error];
        if (library == nil) {
            return fail_error(prog, "compile", error);
        }

        NSArray<NSString *> *names = library.functionNames;
        if (names == nil) {
            fprintf(stderr, "%s: library returned no function names\n", prog);
            return 1;
        }

        error = nil;
        id<MTLBinaryArchive> archive =
            [device newBinaryArchiveWithDescriptor:[MTLBinaryArchiveDescriptor new] error:&error];
        if (archive == nil) {
            return fail_error(prog, "archive", error);
        }

        for (NSString *name in names) {
            const char *fname = name.UTF8String != NULL ? name.UTF8String : "?";
            id<MTLFunction> function = [library newFunctionWithName:name];
            if (function == nil) {
                fprintf(stderr, "%s: missing function %s\n", prog, fname);
                return 1;
            }
            error = nil;
            MTLComputePipelineDescriptor *descriptor = [MTLComputePipelineDescriptor new];
            descriptor.computeFunction = function;
            if (![archive addComputePipelineFunctionsWithDescriptor:descriptor error:&error]) {
                return fail_error(prog, "archive", error);
            }
        }

        NSString *out = [NSString stringWithUTF8String:out_path];
        if (out == nil) {
            fprintf(stderr, "%s: output path is not valid UTF-8\n", prog);
            return 1;
        }
        error = nil;
        if (![archive serializeToURL:[NSURL fileURLWithPath:out] error:&error]) {
            return fail_error(prog, "serialize", error);
        }

        error = nil;
        NSDictionary<NSFileAttributeKey, id> *attrs =
            [[NSFileManager defaultManager] attributesOfItemAtPath:out error:&error];
        if (attrs == nil) {
            return fail_error(prog, "stat", error);
        }

        ProbeReport report;
        memset(&report, 0, sizeof(report));
        report.device_name = device_name;
        report.architecture = architecture;
        report.kernel_count = (unsigned long)names.count;
        report.archive_path = out_path;
        report.archive_bytes = attrs.fileSize;
        return format_report(prog, json, &report);
    }
}
