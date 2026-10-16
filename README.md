# BlinkDB

A Redis-like in-memory key-value store written from scratch in C++.

BlinkDB speaks **RESP2**, the Redis wire protocol, so the standard Redis tools work with it unchanged: `redis-cli` and `redis-benchmark`. It runs as a single thread with a non-blocking `poll()` event loop, and it supports pipelining.

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

Start BlinkDB:

```bash
g++ -std=c++17 -O2 server.cpp -o /tmp/server && /tmp/server --stats > /dev/null
```

Start Redis:

```bash
redis-server --port 6380 --save "" --appendonly no
```

Benchmark BlinkDB (port 1234) and Redis (port 6380) with the same command:

```bash
redis-benchmark -p 1234 -t set,get -n 10000000 -c 1000 -P 64 -d 1024 -r 100000
redis-benchmark -p 6380 -t set,get -n 10000000 -c 1000 -P 64 -d 1024 -r 100000
```

For the 1M runs, use `-n 1000000`.

| | BlinkDB SET | Redis SET | BlinkDB GET | Redis GET | BlinkDB SET | Redis SET | BlinkDB GET | Redis GET |
|---|---|---|---|---|---|---|---|---|
| Total requests | 1M | 1M | 1M | 1M | 10M | 10M | 10M | 10M |
| Concurrent clients | 1,000 | 1,000 | 1,000 | 1,000 | 1,000 | 1,000 | 1,000 | 1,000 |
| Payload | 1 KB | 1 KB | 1 KB | 1 KB | 1 KB | 1 KB | 1 KB | 1 KB |
| Throughput | 761K/s | 1.03M/s | 1.40M/s | 1.25M/s | 795K/s | 1.10M/s | 1.40M/s | 1.20M/s |
| Total time | 1.31 s | 0.97 s | 0.72 s | 0.80 s | 12.57 s | 9.11 s | 7.16 s | 8.32 s |
| Avg latency | 42.4 ms | 19.4 ms | 43.8 ms | 45.5 ms | 39.3 ms | 17.7 ms | 45.6 ms | 46.8 ms |
| Min latency | 12.4 ms | 7.8 ms | 15.2 ms | 10.1 ms | 8.5 ms | 8.8 ms | 3.7 ms | 12.6 ms |
| p50 | 39.0 ms | 18.4 ms | 42.7 ms | 44.9 ms | 38.9 ms | 16.5 ms | 44.3 ms | 47.3 ms |
| p95 | 77.5 ms | 27.9 ms | 46.4 ms | 64.6 ms | 49.9 ms | 25.9 ms | 53.1 ms | 58.7 ms |
| p99 | 97.9 ms | 36.0 ms | 86.7 ms | 72.1 ms | 65.3 ms | 27.8 ms | 71.9 ms | 68.7 ms |
| p99.99 | 107 ms | 41.4 ms | 92.1 ms | 75.0 ms | 115 ms | 61.0 ms | 74.8 ms | 90.9 ms |
| Max latency | 107 ms | 41.4 ms | 92.1 ms | 75.0 ms | 116 ms | 61.3 ms | 74.8 ms | 92.0 ms |

The raw output of every run is in `results/`.

## Known limitations

- **No persistence.** All data is lost when the server stops.
- **Only 4 commands.** There is no expiry (TTL) and only strings are stored. Inline commands aren't supported, so `redis-benchmark -t ping` fails on its `PING_INLINE` test.
- **IPv4 only.** Connect to `127.0.0.1`, not `localhost`, which macOS resolves to the IPv6 address `::1`.
- **`poll()` scans every socket** on each loop iteration. kqueue (macOS) and epoll (Linux) report only the sockets that are ready.
- **A per-request `erase`.** Removing each handled request from the front of the input buffer moves the remaining bytes every time. This is expensive with big pipelined values.

## Roadmap

- [ ] Track a read position and trim the input buffer once per batch, instead of once per request
- [x] Incremental rehashing
- [ ] Use kqueue/epoll instead of poll()
- [ ] Expiry (`EXPIRE`, `TTL`) and more commands
- [ ] IPv6 support
- [ ] Persistence (snapshots or an append-only log)
