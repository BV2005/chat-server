# Chat Server

A multi-user TCP chat server and console client for Windows, written in C++20 on Winsock. The server is built on
**I/O completion ports (IOCP)** with `AcceptEx`, so a small pool of worker threads serves many connections
asynchronously. Clients log in (new usernames are registered automatically), send private messages, and join chat
rooms to talk to groups.

## Features

- **Asynchronous I/O:** IOCP with 4 worker threads; 16 `AcceptEx` calls kept pending so new connections are accepted
  without blocking; overlapped `WSARecv` / `WSASend` throughout.
- **Binary protocol:** length-prefixed packets (maximum 64 KiB) with a streaming parser that handles partial and
  coalesced TCP reads and drops connections that send malformed data.
- **Accounts:** log in with a username and password; an unknown username is registered on first login
  (stored in `chat_users.txt` next to the server). One active session per username.
- **Private messages** between logged-in users and **chat rooms** (join, leave, post to members).
- **Ordered, non-blocking sends:** one send in flight per client, the rest queued; partial sends are resumed.
- **Abuse limits:** 120 messages per minute per client, up to 20 rooms per client, input validation on usernames and
  passwords, and the server always sets the sender itself, so clients cannot impersonate other users.
- **Timeouts:** connections that have not logged in are closed after 30 s, idle logged-in connections after 2 min.
- **Clean shutdown** on Ctrl+C: stops accepting, disconnects clients and joins its threads.

## Build

Requirements: Windows, CMake 3.20+, and a C++20 compiler (MSVC from Visual Studio 2022, or MinGW-w64 as bundled with
CLion).

```powershell
cmake -S . -B build
cmake --build build --config Release
```

This builds two programs:

| Program | Source | Purpose |
|---|---|---|
| `tcp_server` | `TCP_SERVER.cpp`, `Router.cpp`, `Room_manager.cpp`, `Client_create_and_manage.cpp`, `packet.cpp` | the chat server |
| `tcp_client` | `Chat_client.cpp`, `packet.cpp` | the console client |

With the Visual Studio generator the executables are in `build\Release\`; with Ninja or MinGW Makefiles (CLion's
default) they are in `build\`. In CLion, open the folder and run the `tcp_server` / `tcp_client` configurations.

## Run

Start the server with the port to listen on:

```powershell
.\build\Release\tcp_server.exe 5000
```
```
Server listening on port 5000 (new accounts are saved to chat_users.txt)
```

Start one client per user (in separate terminals). Without arguments the client connects to `127.0.0.1:5000`:

```powershell
.\build\Release\tcp_client.exe                   # 127.0.0.1:5000
.\build\Release\tcp_client.exe 192.168.1.20 5000 # another host and port
```

The client asks for a username (1-32 characters: letters, digits, `_`, `-`) and a password (1-128 characters, no tabs
or line breaks). A new username is registered with that password; an existing one must match it.

### Client commands

| Command | Action |
|---|---|
| `/msg <user> <text>` | private message to a logged-in user |
| `/join <room>` | join a room (created on first join) |
| `/group <room> <text>` | message everyone in a room you have joined |
| `/leave <room>` | leave a room |
| `/quit` | log out and exit |

Example, with `alice` and `bob` logged in:

```
alice> /join general
bob>   /join general
bob>   /group general hi everyone
alice  [general] bob: hi everyone
alice> /msg bob hello
bob    [private] alice: hello
```

Errors from the server (unknown recipient, not a member of the room, rate limit, ...) are printed as
`Server error: <reason>`.

## Protocol

Every packet is a 12-byte header followed by a body. Integers are written in the machine's byte order
(little-endian on x86/x64), so the server and clients must use the same architecture.

```
header (12 bytes):  uint32 payload_length | uint32 type | uint32 flags
body:               uint32 sender_len | uint32 recipient_len | sender | recipient | payload
```

`payload_length` is the body size (at least 8 bytes, at most 64 KiB). Packet types:

| Value | Type | Sent by | Fields |
|---|---|---|---|
| 0 | `LOGIN` | client | `payload` = username, `sender` = password |
| 1 | `LOGIN_REPLY` | server | `payload` = message; `flags` bit 0 = login succeeded |
| 2 | `PRIVATE_MESSAGE` | both | `recipient` = username, `payload` = text (server fills in `sender`) |
| 3 | `GROUP_MESSAGE` | both | `recipient` = room, `payload` = text (server fills in `sender`) |
| 4 | `JOIN_ROOM` | client | `recipient` = room |
| 5 | `LEAVE_ROOM` | client | `recipient` = room |
| 6 | `LOGOUT` | client | - |
| 7 | `HEARTBEAT` | client | - (keeps an idle connection alive) |
| 8 | `ERROR_MESSAGE` | server | `payload` = reason, `sender` = `server` |

A packet with an unknown type, an out-of-range length or inconsistent field lengths closes the connection.

## Code structure

| File | Responsibility |
|---|---|
| `TCP_SERVER.cpp/.h` | entry point; IOCP setup, `AcceptEx`, worker threads, receive handling, timeouts, shutdown |
| `Client_create_and_manage.cpp/.h` | `Client` (socket, send queue, rate limit, activity time) and `Client_Manager` (connections and usernames, thread-safe) |
| `Router.cpp/.h` | dispatches each packet to its handler: login and registration, messages, rooms, logout, heartbeat |
| `Room_manager.cpp/.h` | room membership and broadcasting |
| `packet.cpp/.h` | packet format, serialisation and the streaming parser |
| `Chat_client.cpp/.h` | the console client |

## Limitations

- **Windows only** (Winsock, IOCP).
- **Passwords are stored in plain text** in `chat_users.txt`, and the protocol has **no encryption** (no TLS): use it
  on trusted networks only.
- The server prints every received packet, including message text, to its console.
- The client does not send heartbeats, so a client that is idle for more than 2 minutes is disconnected.
- Messages are not stored: users who are offline or not in a room do not receive them later.
