# TCP Chat Server

A multi-client chat server written in C, built from raw BSD sockets with `poll()`-based I/O multiplexing — no external networking libraries. Built as a hands-on project to learn socket programming, event-driven server design, and the C standard library at a low level.

## Features

- Multi-client support — any number of clients can connect and chat simultaneously, handled by a single process using `poll()`.
- Username system — clients set a name on connect, with server-side uniqueness enforcement.
- Commands:
  - `/username <name>` — set your username on connect, or rename yourself mid-session.
  - `/list` — show everyone currently connected.
  - `/disconnect` — leave the chat cleanly.
- Join/leave broadcasts, so everyone knows who's in the room.
- Server-side event logging (connections, disconnections, username changes) to the server's own console.
- A custom `client.c` — a proper non-blocking terminal client that can send and receive simultaneously.
- Tested over a real network (not just localhost) via port forwarding — see below.
- CI pipeline (GitHub Actions) that compiles both `server.c` and `client.c` on every push.

## Build & run

```
gcc server.c -o server -Wall
gcc client.c -o client -Wall
```

Start the server:
```
./server
```

Connect a client (in a separate terminal):
```
./client localhost
```

Multiple clients can connect simultaneously, each in its own terminal window.

## Usage

On connecting, you'll be prompted to set a username:
```
Enter a username: /username Amr
```
From there, anything you type is broadcast to everyone else as a chat message, unless it starts with `/`, in which case it's interpreted as a command.

## Architecture

The server is single-threaded and event-driven, built around `poll()`:

- One listening socket, plus one socket per connected client, are tracked in a `struct pollfd` array.
- A parallel `Client` array (same index as the `pollfd` array) tracks each connection's username and whether it's been set yet.
- `poll()` blocks until *any* tracked socket has activity — a new connection on the listener, or data from an existing client — and the server handles whichever occurred without blocking on any one connection. This is what lets one process serve many clients at once, and it's why a naive blocking-`recv()`-in-a-loop design wouldn't work here.
- On disconnect (or `/disconnect`), the client's slot is removed from both arrays via a swap-and-shrink (the last element is moved into the freed slot rather than shifting the whole array), and the *current* loop index is deliberately re-checked rather than advanced past, since the swap means new data can land in a slot that's already been "seen" this pass.

**A deliberate design choice worth calling out:** the server's protocol is plain text over TCP, one line per message. This means the server works correctly with *any* raw TCP client — it was tested successfully against `telnet`, a generic TCP terminal app, and the purpose-built `client.c` alike. The custom client isn't required for the protocol to work; it exists to demonstrate a properly built non-blocking client (its own `poll()` loop watching both the socket and stdin at once), which is a separate piece of engineering from the server itself.

## Design decisions

- **Usernames are set once per connection and stored server-side**, keyed to the socket. The server, not the client, decides who's speaking — every broadcast message is prefixed with the name the server has on file for that connection, never something the client claims inline. This prevents a client from spoofing another user's name mid-session.
- **Any message starting with `/` is always treated as a command attempt**, even if the word after it isn't recognized (you'll get "unknown command" rather than the text being sent as a chat message). This matches how IRC and most real chat protocols handle the slash prefix, and avoids ambiguity between failed commands and deliberate messages.
- **Usernames are ephemeral**, tied only to the live connection. Disconnecting and reconnecting is a fresh identity with no memory of the previous session — there's no persistent account system.

## Real-network testing

Beyond local testing across multiple terminals, the server was tested across a genuine home Wi-Fi network — a phone running a generic TCP/telnet app connected to the server (running in WSL on a laptop) over real network routing, not `localhost`. This required:

1. Finding WSL's internal IP address (`ip addr`), since WSL sits behind its own NAT layer, invisible to the rest of the local network by default.
2. Setting up Windows port forwarding (`netsh interface portproxy`) to route incoming traffic on the host machine's real network address into WSL.
3. Opening the port in Windows Firewall.
4. Connecting from the second device to the host machine's real Wi-Fi IP.

Two-way chat between the phone and a WSL-based terminal client was confirmed working correctly.

## Continuous Integration

Every push to `main` triggers a GitHub Actions workflow (`.github/workflows/build.yml`) that compiles both `server.c` and `client.c` with `-Wall`, catching build breakage automatically before it's discovered by anyone trying to run the code.

## Known limitations

- **`poll()` rather than `epoll()`.** `poll()` scales fine at this project's size (a handful of clients) but does an O(n) scan of all tracked sockets on every call — real production servers handling thousands of connections use `epoll()` (Linux) or `kqueue` (BSD/macOS) for O(1)-ish, event-driven scaling instead. `poll()` was chosen here deliberately for portability (POSIX-standard, works everywhere) and because it's the more approachable API to learn the underlying mechanics on.
- **Broadcast doesn't check individual `send()` failures.** If one recipient's connection has silently died but hasn't yet been detected by `poll()`, a broadcast to that client fails quietly rather than being logged or triggering cleanup. Fixing this correctly requires `broadcast()` to report which sockets failed back to the caller (rather than removing them inline, which would risk invalidating the outer loop's index).
- **No message encryption** — this is a plain-text protocol over an unencrypted TCP connection, appropriate for a learning project on a local/trusted network, not for anything handling sensitive data.