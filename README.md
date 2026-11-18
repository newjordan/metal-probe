# metal-probe

A small macOS command-line tool that reports which Metal device and GPU
architecture a machine has, then builds a `.metallib` binary archive from a
one-kernel Metal source string. Useful for checking what a GitHub Actions
macOS runner (or any Mac) exposes to Metal.

## Build

Requires macOS with the Metal framework and a C compiler:

    clang -fobjc-arc -framework Metal -framework Foundation probe.m -o probe

## Run

    ./probe out.metallib

Prints the device name, architecture name, and kernel count, then writes the
binary archive to the given path and reports its size. Pass the output file
path as the only argument; the tool exits non-zero if there is no Metal
device or if compilation or serialization fails.

The archive is built with `MTLBinaryArchive`, so it only caches pipelines for
the GPU family of the machine it runs on.
