#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

int main(int argc, char **argv) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        printf("device name: %s\n", [[dev name] UTF8String]);
        MTLArchitecture *a = [dev architecture];
        printf("arch name: %s\n", [[a name] UTF8String]);
        printf("arch family: %lu\n", (unsigned long)[a family]);
        printf("maxThreadsPerThreadgroup: %lu\n", (unsigned long)[dev maxThreadsPerThreadgroup]);

        NSString *src = @"#include <metal_stdlib>\n"
                         "using namespace metal;\n"
                         "kernel void k(device float* o [[buffer(0)]], uint gid [[thread_position_in_grid]]) { o[gid] = 1.0f; }\n";
        NSError *e = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:[MTLCompileOptions new] error:&e];
        if (!lib) { printf("lib err: %s\n", [[e localizedDescription] UTF8String]); return 1; }
        printf("kernel count: %lu\n", (unsigned long)[[lib functionNames] count]);

        id<MTLBinaryArchive> arch = [dev newBinaryArchiveWithDescriptor:[MTLBinaryArchiveDescriptor new] error:&e];
        for (NSString *k in [lib functionNames]) {
            MTLComputePipelineDescriptor *cpd = [MTLComputePipelineDescriptor new];
            cpd.computeFunction = [lib newFunctionWithName:k];
            if (![arch addComputePipelineFunctionsWithDescriptor:cpd error:&e]) {
                printf("arch err: %s\n", [[e localizedDescription] UTF8String]); return 1;
            }
        }
        NSString *out = [NSString stringWithUTF8String:argv[1]];
        if (![arch serializeToURL:[NSURL fileURLWithPath:out] error:&e]) {
            printf("serialize err: %s\n", [[e localizedDescription] UTF8String]); return 1;
        }
        NSDictionary *at = [[NSFileManager defaultManager] attributesOfItemAtPath:out error:&e];
        printf("wrote %s (%llu bytes)\n", argv[1], (unsigned long long)[at fileSize]);
    }
    return 0;
}
