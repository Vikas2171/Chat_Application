#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include <iostream>
#include <queue>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <fstream>
#include <ctime>
#include <memory>

#include "Framing.h"

using namespace std;

#define PORT 9909
#define SIZE 1024
#define MAX_CLIENTS 5

// ---------------------------------------------------------------------------
// Per-client state.
//
// Each connected client owns a reader thread (blocking recv loop) and a
// writer thread (drains outQueue and does the actual send()). Reader threads
// never call send() on behalf of another client directly - they push onto
// that client's outQueue and notify its cv, so one slow/stalled client can
// only ever block its own writer thread, never the broadcaster.
// ---------------------------------------------------------------------------
struct ClientState {
    SOCKET socket;
    int id;

    mutex outMutex;
    condition_variable outCv;
    queue<string> outQueue;

    thread reader;
    thread writer;

    LineFramer inFramer; // only ever touched by this client's own reader thread - no lock needed

    atomic<bool> tearingDown{ false }; // guards against double cleanup (normal disconnect vs. shutdown)

    ClientState(SOCKET s, int clientId) : socket(s), id(clientId) {}
};

static mutex g_clientsMutex; // guards g_clients and g_pending only - never held across send()/recv()
static vector<shared_ptr<ClientState>> g_clients;
static queue<SOCKET> g_pending;
static atomic<int> g_nextClientId{ 1 };

static mutex g_logMutex; // guards chatlog.txt, written to concurrently by every reader thread
static ofstream g_logFile("chatlog.txt", ios::app);

static mutex g_coutMutex; // guards cout, written to concurrently by every reader thread

static SOCKET g_listenSocket = INVALID_SOCKET;
static atomic<bool> g_running{ true };

static void logToConsole(const string& line) {
    lock_guard<mutex> lock(g_coutMutex);
    cout << line << endl;
}

static string timestamp() {
    time_t now = time(nullptr);
    char buf[32];
    tm localTm;
    localtime_s(&localTm, &now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTm);
    return string(buf);
}

static void logLine(const string& line) {
    lock_guard<mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile << line << endl;
    }
}

// Forward declarations
static void clientReaderLoop(shared_ptr<ClientState> client);
static void clientWriterLoop(shared_ptr<ClientState> client);
static void removeClient(shared_ptr<ClientState> client);
static void promotePendingLocked(); // caller must hold g_clientsMutex
static void startClientThreads(shared_ptr<ClientState> client);

// Push a message onto every connected client's outgoing queue except sender's.
static void broadcastMessage(const string& msg, int senderId) {
    lock_guard<mutex> lock(g_clientsMutex);
    for (auto& client : g_clients) {
        if (client->id == senderId) continue;
        {
            lock_guard<mutex> outLock(client->outMutex);
            client->outQueue.push(msg);
        }
        client->outCv.notify_one();
    }
}

static void startClientThreads(shared_ptr<ClientState> client) {
    string welcome = "Got the connection done successfully.\n";
    send(client->socket, welcome.c_str(), (int)welcome.size(), 0);

    client->reader = thread(clientReaderLoop, client);
    client->writer = thread(clientWriterLoop, client);
    client->reader.detach();
    client->writer.detach();

    logToConsole("[Client " + to_string(client->id) + "] connected.");
}

// Promote a pending connection into a freed slot. Caller must hold g_clientsMutex.
static void promotePendingLocked() {
    if (g_pending.empty() || (int)g_clients.size() >= MAX_CLIENTS) return;

    SOCKET clientSocket = g_pending.front();
    g_pending.pop();

    int id = g_nextClientId++;
    auto client = make_shared<ClientState>(clientSocket, id);
    g_clients.push_back(client);

    startClientThreads(client);
}

// Remove a client from the active list and wake its writer so it can exit.
// Safe to call from the client's own reader thread (normal disconnect) or
// from the shutdown coordinator - tearingDown ensures only one path acts.
static void removeClient(shared_ptr<ClientState> client) {
    bool expected = false;
    if (!client->tearingDown.compare_exchange_strong(expected, true)) {
        return; // already being torn down by someone else
    }

    closesocket(client->socket);

    {
        lock_guard<mutex> lock(g_clientsMutex);
        for (size_t i = 0; i < g_clients.size(); i++) {
            if (g_clients[i]->id == client->id) {
                g_clients.erase(g_clients.begin() + i);
                break;
            }
        }
        promotePendingLocked();
    }

    client->outCv.notify_all(); // wake the writer thread so it can exit

    logToConsole("[Client " + to_string(client->id) + "] disconnected.");
}

static void clientReaderLoop(shared_ptr<ClientState> client) {
    char buff[SIZE];
    while (g_running) {
        memset(buff, 0, SIZE);
        int nRet = recv(client->socket, buff, SIZE, 0);

        if (nRet == SOCKET_ERROR) {
            // If tearingDown is already set, this recv() failure is simply
            // the expected result of removeClient()/shutdownServer() closing
            // our socket out from under us - not a genuine network error.
            if (!client->tearingDown) {
                int error = WSAGetLastError();
                logToConsole("[Client " + to_string(client->id) + "] recv() failed with error: " + to_string(error));
            }
            break;
        }
        if (nRet == 0) {
            break; // connection closed gracefully by client
        }

        // A single recv() can contain a partial message, several messages
        // back-to-back, or both - TCP has no message boundaries of its own.
        // inFramer reassembles the newline-delimited messages this protocol
        // actually sends.
        for (const string& text : client->inFramer.feed(buff, nRet)) {
            string tagged = "[Client " + to_string(client->id) + "]: " + text;

            logLine(timestamp() + " | Client " + to_string(client->id) + " | " + text);
            logToConsole(tagged);

            broadcastMessage(tagged, client->id);
        }
    }

    removeClient(client);
}

static void clientWriterLoop(shared_ptr<ClientState> client) {
    while (true) {
        unique_lock<mutex> lock(client->outMutex);
        client->outCv.wait(lock, [&] {
            return !client->outQueue.empty() || client->tearingDown.load();
            });

        if (client->outQueue.empty() && client->tearingDown) {
            break; // shutting down and nothing left to flush
        }

        string msg = client->outQueue.front() + "\n"; // frame with the delimiter the protocol expects
        client->outQueue.pop();
        lock.unlock();

        send(client->socket, msg.c_str(), (int)msg.size(), 0);
    }
}

// Single entry point for triggering shutdown, safe to call from any thread
// (the watcher thread, a Ctrl+C handler, or main() itself). Only the first
// caller actually closes the listening socket - this is what unblocks the
// main thread's blocking accept() call so it can notice g_running is false
// and fall through to shutdownServer() for the rest of the teardown.
static void requestShutdown() {
    bool expected = true;
    if (g_running.compare_exchange_strong(expected, false)) {
        if (g_listenSocket != INVALID_SOCKET) {
            closesocket(g_listenSocket);
            g_listenSocket = INVALID_SOCKET;
        }
    }
}

// Watches the server console for a "quit" command and triggers shutdown.
static void shutdownWatcherLoop() {
    string line;
    while (g_running) {
        if (!getline(cin, line)) break;
        if (line == "quit") {
            requestShutdown();
            break;
        }
    }
}

static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        requestShutdown();
        return TRUE;
    }
    return FALSE;
}

static void shutdownServer() {
    cout << "Shutting down..." << endl;
    requestShutdown(); // no-op if already triggered by the watcher/Ctrl+C

    vector<shared_ptr<ClientState>> clientsSnapshot;
    {
        lock_guard<mutex> lock(g_clientsMutex);
        clientsSnapshot = g_clients; // copy so we can act without holding the lock
    }

    for (auto& client : clientsSnapshot) {
        removeClient(client); // shuts down the socket and unblocks its reader's recv()
    }

    // Reader/writer threads are detached (their lifetime is tied to the
    // ClientState shared_ptr, not to main()), so there is nothing left to
    // join here - removeClient() already unblocked them and they exit on
    // their own once their recv()/wait() returns.
    Sleep(200); // brief grace period for detached threads to finish exiting before WSACleanup

    if (g_logFile.is_open()) g_logFile.close();
    WSACleanup();
}

int main() {
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    WSADATA ws;
    if (WSAStartup(MAKEWORD(2, 2), &ws) != 0) {
        cout << "WSA initialization failed." << endl;
        return EXIT_FAILURE;
    }
    cout << "WSA initialized." << endl;

    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET) {
        cout << "Socket not opened." << endl;
        WSACleanup();
        return EXIT_FAILURE;
    }
    cout << "Socket opened successfully. Socket Descriptor: " << g_listenSocket << endl;

    struct sockaddr_in srv;
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    srv.sin_addr.s_addr = INADDR_ANY;
    memset(&(srv.sin_zero), 0, 8);

    int nOptVal = 1;
    if (setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOptVal, sizeof(nOptVal)) != 0) {
        cout << "setsockopt call failed." << endl;
        WSACleanup();
        return EXIT_FAILURE;
    }

    if (bind(g_listenSocket, (sockaddr*)&srv, sizeof(sockaddr)) != 0) {
        cout << "Failed to bind to local port." << endl;
        WSACleanup();
        return EXIT_FAILURE;
    }
    cout << "Successfully bound to local port." << endl;

    if (listen(g_listenSocket, MAX_CLIENTS) != 0) {
        cout << "Failed to start listening on local port." << endl;
        WSACleanup();
        return EXIT_FAILURE;
    }
    cout << "Started listening on local port " << PORT << "." << endl;
    cout << "Type \"quit\" and press Enter at any time to shut the server down gracefully." << endl;

    thread watcher(shutdownWatcherLoop);
    watcher.detach();

    while (g_running) {
        int nLen = sizeof(struct sockaddr);
        SOCKET clientSocket = accept(g_listenSocket, NULL, &nLen);

        if (clientSocket == INVALID_SOCKET) {
            if (!g_running) break; // accept() unblocked by shutdownServer() closing g_listenSocket
            logToConsole("accept() failed with error: " + to_string(WSAGetLastError()));
            continue;
        }

        bool spaceAvailable;
        shared_ptr<ClientState> newClient;
        {
            lock_guard<mutex> lock(g_clientsMutex);
            spaceAvailable = (int)g_clients.size() < MAX_CLIENTS;
            if (spaceAvailable) {
                int id = g_nextClientId++;
                newClient = make_shared<ClientState>(clientSocket, id);
                g_clients.push_back(newClient);
            }
            else {
                g_pending.push(clientSocket);
            }
        }

        if (spaceAvailable) {
            startClientThreads(newClient);
        }
        else {
            logToConsole("New connection is pending (server full).");
            string fullMsg = "No space available on the server. Your connection is pending.\n";
            send(clientSocket, fullMsg.c_str(), (int)fullMsg.size(), 0);
        }
    }

    shutdownServer();
    return 0;
}
