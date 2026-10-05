# FluxC

FluxC is a small C11 library for low-latency and embedded systems. It provides
simple building blocks with explicit memory ownership and no hidden allocations.
The library can be used from C and C++ applications.

- Lock-free single-producer/single-consumer queues with a typed C++ wrapper
- Fixed-size memory pools
- Latency histograms
- Monotonic timing

## Build

Requires a C11 compiler and CMake 3.16 or newer.

```sh
cmake -S . -B build
cmake --build build
```

## License

[MIT](LICENSE)
