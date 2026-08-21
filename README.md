# Chat Application (Server-Client Model)

## Introduction
This project is a multi-client chat application demonstrating a server-client model in C++ using raw TCP sockets (Winsock) and multithreading. The server accepts multiple concurrent client connections, broadcasts messages between them, and handles disconnections and shutdown gracefully. The client connects to the server and lets the user send and receive messages concurrently.

## Project Structure
* __Server Code (`Server.cpp`):__ Accepts client connections, broadcasts messages, logs chat history, and manages graceful shutdown.
* __Client Code (`Client.cpp`):__ Connects to the server, sends messages typed by the user, and receives broadcasts.

## Features
* Multiple clients can connect to the server simultaneously (`MAX_CLIENTS`, default 5).
* Messages are broadcast to all other connected clients, tagged with the sender's client ID.
* Pending connection requests are queued when the server is full and promoted automatically as slots free up.
* Chat history is logged to `chatlog.txt` with a timestamp and client ID for every message.
* Graceful shutdown on both ends: type `quit` (or press Ctrl+C on the server) instead of just killing the process.

## Architecture

### Thread-per-client, not `select()`
The server used to multiplex all clients on a single thread using `select()`. It now uses a **thread-per-client** model instead: the main thread only `accept()`s new connections; each connected client gets its own dedicated **reader thread** running a blocking `recv()` loop.

### Per-client outgoing queue + writer thread
A naive thread-per-client broadcast would have each reader thread call `send()` directly on every *other* client's socket. That means one slow or stalled client can block message delivery to everyone else, since the sender's thread would sit blocked inside `send()` on that one bad socket.

Instead, each client also has a dedicated **writer thread** and its own outgoing queue (`queue<string>` + `mutex` + `condition_variable`). When a reader thread wants to broadcast a message, it pushes the message onto every other client's queue and notifies that client's writer thread — it never calls `send()` on another client's socket itself. Each writer thread drains its own queue and does the actual `send()`. This means a slow client can only ever back up its *own* queue, never block delivery to anyone else.

### Locking model
Three narrow, single-purpose locks, none of them ever held across a blocking `send()`/`recv()` call:
* `clientsMutex` — guards the connected-client list and the pending-connection queue only (add/remove/lookup).
* A per-client `outMutex`/`condition_variable` — guards that one client's outgoing queue, for its writer thread to wait on.
* `logMutex` — guards writes to `chatlog.txt`, since multiple reader threads log concurrently and `ofstream` isn't thread-safe for that.
* `coutMutex` — guards console output for the same reason; without it, concurrent client threads print to the console at the same time and lines interleave mid-word.

### Client identity
Each connection gets a monotonically increasing integer `clientId` assigned at accept time, used for broadcast tags (`[Client <id>]: <message>`), console logging, and the chat log — rather than the raw `SOCKET` handle, which gets reused after `closesocket()` and would let a new connection inherit a stale identity.

### Graceful shutdown
Typing `quit` at the server console, or pressing Ctrl+C, triggers a single-owner `requestShutdown()`:
1. Closes the listening socket — this is what unblocks the main thread's blocking `accept()` call.
2. Closes every connected client's socket — this unblocks each one's blocked `recv()` call in its reader thread.
3. A `tearingDown` flag per client prevents the shutdown path and a client's own normal-disconnect path from racing to double-close the same socket.
4. Notifies every writer thread so it wakes up and exits instead of waiting on its queue forever.

The client side has the same category of fix: the receive thread used to be `detach()`ed, which meant it could still be running when the main thread's send-error path called cleanup concurrently — a real race between the two, and the reason `detach()` was replaced with a `running` flag + `join()`, unblocked the same way (`shutdown()`/`closesocket()` from whichever side notices the disconnect first).

## Known Limitations
These are deliberate, understood trade-offs for a learning/demo project, not oversights:
* __Fixed client cap:__ `MAX_CLIENTS` is a compile-time constant; the server doesn't scale beyond it, only queues beyond it.
* __Plaintext, no confidentiality from the server:__ There's no TLS/encryption. Because this is a relay-based chat server, every message necessarily passes through the server in plaintext to be broadcast and logged — the server operator (or anyone reading `chatlog.txt`) can read every conversation. True end-to-end privacy would require client-side encryption, which is out of scope here.
* __Localhost-oriented:__ The client's server address is hardcoded to `127.0.0.1`.
* __One extra keypress to exit if the server dies first:__ If the server shuts down while a client is connected, the client's receiver thread detects it immediately and prints `Connection closed by server.`, but the client's main thread may still be blocked waiting on keyboard input (`fgets`). There's no portable way to interrupt a blocked console read from another thread, so the client process stays alive-but-idle until the next keypress, at which point it notices and exits cleanly.
* __Console output can occasionally interleave with your own typing:__ the OS echoes your keystrokes to the console as you type, independently of the program; if an incoming broadcast message happens to print at the exact moment you're mid-keystroke on your own line, the two can visually interleave (e.g. `hilient 1]: hello`). This doesn't corrupt or lose what you actually typed — it's purely a display collision, and it depends on timing, so it won't happen on every message. Fixing it properly would mean replacing the simple blocking `fgets()` input with raw keystroke handling and a line-redraw system (effectively a small readline implementation), which is a meaningfully larger feature than anything else in this project and was deliberately left out of scope.
* __At real Windows scale, this would use IOCP:__ thread-per-connection works fine at the small scale this project targets, but doesn't scale to large numbers of concurrent connections — each connection costs two OS threads. A production Windows service handling this at scale would use I/O Completion Ports (IOCP) instead, which lets a small, fixed pool of threads service many thousands of connections without a thread per client. Thread-per-connection was the right choice here for clarity and scope, not because it's the production-grade pattern.

## Technologies Used
* __C++ (C++14):__ The primary language for both server and client.
* __Winsock API:__ Socket programming for network communication.
* __Multithreading (`std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`):__ Thread-per-client on the server, concurrent send/receive on the client.
* __Standard Library:__ `queue`, `vector`, `string`, `fstream`, etc.

## Development Environment
This project can be built and debugged either way:

### Option 1: Visual Studio
Open the `.cpp` files in Visual Studio, add `wsock32.lib` under Project Properties → Linker → Input, and build/run as usual.

### Option 2: VS Code
This repo includes a ready-to-use VS Code setup under `.vscode/` that drives the same MSVC toolchain (`cl.exe`) Visual Studio uses — no need to install a separate compiler:
* `Ctrl+Shift+B` → **Build Server (MSVC)**, or Terminal → Run Task… → **Build Client (MSVC)**.
* `F5` builds and launches either target with full MSVC debugging (`cppvsdbg`) — breakpoints, thread inspection, call stacks all work.
* Requires Visual Studio Build Tools (or Visual Studio) with the C++ workload installed; the build script locates it automatically via `vswhere`.

## How To Run
1. __Server:__ Build and run `Server.exe`. It will start listening on port 9909, and print `Type "quit" and press Enter at any time to shut the server down gracefully.`
2. __Client:__ Build and run `Client.exe` (multiple instances to simulate multiple users). Each instance connects to the server automatically.
3. __Chat:__ Type a message and press Enter in any client to broadcast it to the others. Type `quit` in a client to disconnect just that client, or `quit` at the server console to shut the whole server down.

## Example Usage
1. __Start the server.__
![Build The Solution](https://github.com/Vikas2171/Chat_Application/blob/main/Photos/1.jpg "Build The Solution")
![Run The Solution](https://github.com/Vikas2171/Chat_Application/blob/main/Photos/2.jpg "Run The Solution")
2. __Run multiple clients the same way as the server.__
3. __Communicate between clients.__
![Communication](https://github.com/Vikas2171/Chat_Application/blob/main/Photos/3.jpg "Communication")

## Conclusions
This project demonstrates a thread-per-connection chat server in C++: concurrent client handling, per-connection message queues to avoid head-of-line blocking on a slow client, coordinated multithreaded shutdown, and basic chat logging. It's a foundational example of the concurrency and synchronization patterns that come up in real network services, along with an honest accounting of where a production system (encryption, IOCP-based scaling) would go further than this project's scope.

Feel free to explore the code and modify it to suit your needs!

## 🚀 About Me
My name is __Vikas Prajapati__, and I am currently pursuing __BTech in Computer Science and Engineering from IIT Jammu__. This project is part of my ongoing efforts to explore and understand network programming and concurrent processing using C++.

## 🔗 Links
[![linkedin](https://img.shields.io/badge/linkedin-0A66C2?style=for-the-badge&logo=linkedin&logoColor=white)](https://www.linkedin.com/in/vikas-prajapati-577bab252/)
