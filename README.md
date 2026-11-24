# Chat Server

A Windows TCP chat server and client written in C++20, built with CMake and Winsock.

## Prerequisites

- CMake 3.20 or newer
- A C++20 compiler on Windows (for example, MSVC)

## Build

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

The project produces:

- `tcp_server` — the chat server
- `tcp_client` — the chat client

## Run

Run the server, then connect a client to it using the configured host and port.

```powershell
.\build\Debug\tcp_server.exe
.\build\Debug\tcp_client.exe
```

See the programs' command-line usage prompts for their required arguments.

## Repository hygiene

Build output, CLion settings, executables, and local chat-user data are intentionally excluded from version control.
