# Benchmark Notes

The included load client is intentionally small. It is useful for smoke-testing concurrent connections and request throughput, not for publishing final performance numbers.

```bash
./build/rss_server 0.0.0.0 7777 4
./build/rss_load_test_client 127.0.0.1 7777 1000 100
```

Record at least:

- CPU model and core count
- OS and kernel version
- build type and compiler version
- worker count
- client count
- messages per client
- total messages sent
- elapsed seconds
- approximate messages per second

Potential follow-up benchmarks:

- room broadcast with many clients in one room
- many rooms with small groups
- large payload rejection
- idle timeout cleanup
- packet fragmentation and coalescing behavior
