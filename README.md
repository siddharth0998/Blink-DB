# BlinkDB

A Redis-like in-memory key-value store written from scratch in C++.

BlinkDB speaks **RESP2**, the Redis wire protocol, so the standard Redis tools work with it unchanged: `redis-cli`, `redis-benchmark` and `memtier_benchmark`. It runs as a single thread with a non-blocking `poll()` event loop, and it supports pipelining.

## Features

- `PING`, `GET`, `SET` and `DEL` (multi-key), with Redis-style replies and error messages
- Commands are case-insensitive; keys and values are binary-safe
- A non-blocking event loop: one thread serves many clients
- Pipelining: a client can send many commands at once and gets the replies in order
- Safety limits on request size, so a broken or hostile client can't make the server use unbounded memory
- `--stats` mode: prints requests/sec and the server's own CPU use (user and system) once per second

## Build and run

You need a C++17 compiler. It's tested on macOS with Apple clang.

```bash
g++ -std=c++17 -O2 server.cpp -o server
./server            # listens on port 1234
./server --stats    # also prints req/s and CPU use every second
```

## Try it with redis-cli

```
$ redis-cli -p 1234
127.0.0.1:1234> set name sid
OK
127.0.0.1:1234> get name
"sid"
127.0.0.1:1234> del name
(integer) 1
127.0.0.1:1234> get name
(nil)
```

## Supported commands

| Command | Reply |
|---|---|
| `PING` | `PONG` |
| `SET key value` | `OK` |
| `GET key` | the value, or `(nil)` if the key doesn't exist |
| `DEL key [key ...]` | how many keys were deleted |

## How it works

| File | What it contains |
|---|---|
| `protocol.h` | The wire format only: the RESP2 request parser and the reply builders. No socket code. |
| `server.cpp` | Sockets, the event loop, the connection buffers and the command handlers |

### The event loop

```
            ┌──────────────────────────────────────────────────┐
            │ poll() waits until some socket is ready          │
            └───────────────┬──────────────────────────────────┘
                            │
      ┌─────────────────────┼─────────────────────────┐
      ▼                     ▼                         ▼
 listening socket      client readable            client writable
 accept() the new      read() → incoming          write() from outgoing
 connection            parse every complete       (switch back to reading
                       request, append the        once everything is sent)
                       replies to outgoing
```

- **Non-blocking sockets.** `read()` and `write()` never wait. If a socket isn't ready, the server moves on to the next client.
- **One buffer pair per connection.** Bytes arrive in pieces, so each connection keeps an `incoming` buffer. The parser reports `PARSE_INCOMPLETE` until a whole request has arrived.
- **Pipelining.** After each read, `while (try_one_message(conn));` handles *every* complete request in the buffer and appends all the replies to `outgoing`. They go back in a single `write()`.

### RESP2 in one example

The client sends `SET name sid` as an array of 3 bulk strings:

```
*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$3\r\nsid\r\n
```

The server replies `+OK\r\n`. Other reply types are `-ERR ...` (error), `:1` (integer), `$3\r\nsid\r\n` (bulk string) and `$-1` (nil).

## Benchmarks: BlinkDB vs Redis

Setup:

- Both servers ran on the same MacBook, over loopback (127.0.0.1). The machine details are in `results/machine.txt`.
- Redis ran with persistence turned off (`--save "" --appendonly no`), because BlinkDB doesn't write to disk either.
- Redis is single-threaded for commands, like BlinkDB.
- Each number comes from a single run, so expect a few percent of noise.

### 1. Realistic workload (memtier_benchmark)

The traffic is shaped like a real cache, not a synthetic one:

- 90% GET and 10% SET, mixed together
- Zipf key popularity (a few hot keys)
- 100,000 keys, loaded before the test
- Value sizes of 100 B (50%), 1 KB (35%) and 10 KB (15%)
- 100 connections, no pipelining, 30 seconds

| | BlinkDB | Redis |
|---|---|---|
| Throughput (ops/sec) | **286,566** | 286,120 |
| Avg latency | 0.349 ms | 0.349 ms |
| p50 | **0.311 ms** | 0.343 ms |
| p99 | 0.719 ms | **0.599 ms** |
| p99.9 | 1.583 ms | **1.119 ms** |
| Data moved | 523 MB/s | 516 MB/s |
| GET misses | 0 | 0 |

BlinkDB matches Redis on throughput and typical latency. Redis has tighter tail latency (p99.9).

### 2. Pipelined, small values (redis-benchmark)

`-c 10 -P 128 -d 16 -r 100000 -n 3000000`

| | BlinkDB SET | Redis SET | BlinkDB GET | Redis GET |
|---|---|---|---|---|
| Requests/sec | **2,737,285** | 2,202,690 | 2,976,254 | **3,191,558** |
| p50 | **0.407 ms** | 0.535 ms | 0.375 ms | **0.359 ms** |
| p99 | 0.871 ms | **0.727 ms** | 0.791 ms | **0.415 ms** |
| max | 6.103 ms | **2.447 ms** | **1.007 ms** | 1.127 ms |

### 3. 1000 clients, 512-byte values (redis-benchmark)

`-c 1000 -P 64 -d 512 -r 100000 -n 1000000`

| | BlinkDB SET | Redis SET | BlinkDB GET | Redis GET |
|---|---|---|---|---|
| Requests/sec | 1,096,491 | **1,293,661** | **1,655,629** | 1,483,680 |
| p50 | 53.5 ms | **24.6 ms** | **36.1 ms** | 38.8 ms |
| p99 | 80.1 ms | **70.7 ms** | **57.7 ms** | 77.2 ms |
| max | 871.9 ms | **78.2 ms** | 595.5 ms | **82.4 ms** |

### What the numbers show

- **Without pipelining, the round trip dominates.** At pipeline 1, both servers reach about 286k ops/sec. That figure is exactly what Little's Law predicts: 100 requests in flight ÷ 0.349 ms. Most of each request's time goes to system calls and the kernel, not to the server's own work.
- **Pipelining gives about a 10× speed-up.** Sending many requests per `read()` and `write()` lets one system call cover the cost of many commands.
- **BlinkDB does less work per command than Redis.** This is why it can be faster on simple GET/SET. Redis also does work BlinkDB skips, such as expiry checks, statistics and LRU tracking.
- **Redis wins on tail latency**, especially with large values and many clients. The likely causes in BlinkDB are listed under known limitations below.

### Raw results

The raw output of every test is in `results/`. To reproduce it, start both servers, then:

```bash
# pipelined, small values
redis-benchmark -p 1234 -t set,get -n 3000000 -c 10 -P 128 -d 16 -r 100000 --csv
# 1000 clients, 512-byte values
redis-benchmark -p 1234 -t set,get -n 1000000 -c 1000 -P 64 -d 512 -r 100000 --csv
# realistic: load the data, then run the mixed workload
memtier_benchmark -s 127.0.0.1 -p 1234 --ratio=1:0 --key-pattern=P:P --key-maximum=100000 --key-prefix="user:" --data-size-list=100:50,1000:35,10000:15 -R -t 4 -c 25 --requests=allkeys --hide-histogram
memtier_benchmark -s 127.0.0.1 -p 1234 --ratio=1:10 --key-pattern=Z:Z --key-maximum=100000 --key-prefix="user:" --data-size-list=100:50,1000:35,10000:15 -R -t 4 -c 25 --test-time=30 --hide-histogram --print-percentiles=50,99,99.9
```

For Redis, run the same commands with `-p 6380`, after starting it with:

```bash
redis-server --port 6380 --save "" --appendonly no
```

## Known limitations

- **No persistence.** All data is lost when the server stops.
- **Only 4 commands.** There is no expiry (TTL) and only strings are stored. Inline commands aren't supported, so `redis-benchmark -t ping` fails on its `PING_INLINE` test.
- **IPv4 only.** Connect to `127.0.0.1`, not `localhost`, which macOS resolves to the IPv6 address `::1`.
- **`poll()` scans every socket** on each loop iteration. kqueue (macOS) and epoll (Linux) report only the sockets that are ready.
- **The hash table rehashes all at once.** When `std::unordered_map` grows, it moves every key in one go, which causes latency spikes. Redis rehashes a little at a time.
- **A per-request `erase`.** Removing each handled request from the front of the input buffer moves the remaining bytes every time. This is expensive with big pipelined values.

## Roadmap

- [ ] Track a read position and trim the input buffer once per batch, instead of once per request
- [ ] Incremental rehashing, or reserving the table size up front
- [ ] Use kqueue/epoll instead of poll()
- [ ] Expiry (`EXPIRE`, `TTL`) and more commands
- [ ] IPv6 support
- [ ] Persistence (snapshots or an append-only log)
