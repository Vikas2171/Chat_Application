// SocketClientProgram.cpp : This file contains the 'main' function. Program execution begins and ends there.

#include <iostream>
#include <winsock.h>
#include <fstream>
#include <thread>

#define PORT 9909
#define SIZE 1024

using namespace std;
int nClientSocket;
struct sockaddr_in srv;
char buff[SIZE] = { 0, };
int nRet = 0;

void receive_file(string filename);

void cleanup() {
    closesocket(nClientSocket);
    WSACleanup();
}

void send_file() {
    // Specify file type
    cout << "Enter 'video', 'image', 'audio', 'text' for sending video, image, audio or text file respectively: ";
    fgets(buff, SIZE, stdin);
    buff[strcspn(buff, "\n")] = '\0';
    send(nClientSocket, buff, SIZE, 0);
    memset(buff, 0, SIZE);

    // Send file to server
    cout << "Enter the file path to send to the server: ";
    string filePath;
    cin >> filePath;

    ifstream file(filePath, ios::binary);
    if (file.is_open()) {
        // Send file content to server
        while (true) {
            file.read(buff, SIZE);
            int size = file.gcount();
            nRet = send(nClientSocket, buff, size, 0);
            if (nRet == SOCKET_ERROR) {
                int error = WSAGetLastError();
                cout << "send() failed with error: " << error << endl;
                if (error == WSAECONNRESET) {
                    cout << "Connection reset by server." << endl;
                }
                cleanup();
                return;
            }
            if (size < SIZE) {
                break;
            }
            memset(buff, 0, SIZE);  // Clear buffer
        }
        file.close();
        cout << "File sent to the server successfully." << endl;
    }
    else {
        cout << "Failed to open file." << endl;
    }
}

void send_message() {
    memset(buff, 0, sizeof(buff));

    // Type text message
    cout << "Enter your message: ";
    fgets(buff, SIZE, stdin);
    buff[strcspn(buff, "\n")] = '\0';

    nRet = send(nClientSocket, buff, strlen(buff), 0);
    if (nRet == SOCKET_ERROR) {
        int error = WSAGetLastError();
        cout << "send() failed with error: " << error << endl;
        if (error == WSAECONNRESET) {
            cout << "Connection reset by server." << endl;
        }
        cleanup();
        return;
    }
    memset(buff, 0, sizeof(buff)); // Clear buffer before receiving response

    // yahan par ham server se response collect kar rahe hain
    // so yahan par hame changes karne honge such that other client ka msg aye server ke through
    //nRet = recv(nClientSocket, buff, 255, 0);
    //if (nRet == SOCKET_ERROR) {
    //    int error = WSAGetLastError();
    //    cout << "recv() failed with error: " << error << endl;
    //    if (error == WSAECONNRESET) {
    //        cout << "Connection reset by server." << endl;
    //    }
    //    cleanup();
    //    return;
    //}
    //else if (nRet == 0) {
    //    // Connection has been gracefully closed
    //    cout << "Connection closed by server." << endl;
    //    cleanup();
    //    return;
    //}
    //else {
    //    // Successfully received data
    //    buff[strcspn(buff, "\n")] = '\0';
    //    cout << "Response from server: " << buff << endl;

    //}
}

void receive_messages() {
    while (true) {
        memset(buff, 0, SIZE);
        nRet = recv(nClientSocket, buff, SIZE, 0);
        // compare initial 6 characters of buff if it is "hello." then we have file
        if (strncmp(buff, "hello.", 6) == 0) {
            string filename = buff;
            receive_file(filename);
        }
        else {
            if (nRet <= 0) {
                cout << "Connection closed by server." << endl;
                cleanup();
                exit(EXIT_FAILURE);
            }
            cout << "Broadcast message: " << buff << endl;
        }
    }
}

void receive_file(string filename) {
    ofstream file(filename, ios::binary | ios::app);
    if (!file.is_open()) {
        cout << "Failed to open file for writing." << endl;
        return;
    }
    cout << "Receiving " << buff << " file from client..." << endl;
    memset(buff, 0, SIZE);

    while (true) {
        int nRet = recv(nClientSocket, buff, SIZE, 0);
        if (nRet == SOCKET_ERROR) {
            int error = WSAGetLastError();
            cout << "recv() failed with error: " << error << endl;
            if (error == WSAECONNRESET) {
                cout << "Connection reset by client." << endl;
            }
            closesocket(nClientSocket);
            file.close();
            remove("received_file.txt"); // Remove incomplete file
            return;
        }
        else if (nRet == 0) {
            cout << "Connection closed by client." << endl;
            closesocket(nClientSocket);
            file.close();
            remove("received_file.txt"); // Remove incomplete file
            break;
        }
        else {
            file.write(buff, nRet);
            if (nRet < SIZE) // Last chunk received
                break;
        }
        memset(buff, 0, SIZE);
    }

    file.close();
    cout << "File received and saved: " << filename << endl;
}

int main() {
    WSADATA ws;
    if (WSAStartup(MAKEWORD(2, 2), &ws) != 0) {
        cout << "WSAStartup failed." << endl;
        return(EXIT_FAILURE);
    }

    nClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (nClientSocket < 0) {
        cout << "socket() call failed." << endl;
        WSACleanup();
        return (EXIT_FAILURE);
    }

    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    srv.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(&srv.sin_zero, 0, 8);

    nRet = connect(nClientSocket, (struct sockaddr*)&srv, sizeof(srv));
    if (nRet < 0) {
        cout << "connect failed." << endl;
        cleanup();
        return (EXIT_FAILURE);
    }
    else {
        cout << "Connected to the server." << endl;

        // Receive initial message from the server
        nRet = recv(nClientSocket, buff, SIZE, 0);
        if (nRet <= 0) {
            cout << "Failed to receive message from server or connection closed." << endl;
            cleanup();
            return EXIT_FAILURE;
        }

        cout << "Message received from the server: " << buff << endl;

        // clearing buffer
        memset(buff, 0, SIZE);

        thread receiver(receive_messages);
        receiver.detach();

        cout << "Enter 'Text' to send text message, 'File' to send file to the server or 'exit' to close connection: " << endl;
        cout << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*" << endl << endl;

        while (true) {
            fgets(buff, SIZE, stdin);
            buff[strcspn(buff, "\n")] = '\0';
            if (strcmp(buff, "Text") == 0) {
                send_message();
            }
            else if (strcmp(buff, "File") == 0) {
                send(nClientSocket, buff, SIZE, 0);
                memset(buff, 0, SIZE);
                send_file();
                nRet = recv(nClientSocket, buff, SIZE, 0);
                if (nRet <= 0) {
                    cout << "Failed to receive message from server or connection closed." << endl;
                    cleanup();
                    return EXIT_FAILURE;
                }

                cout << buff << endl;
                memset(buff, 0, SIZE);
            }
            else if (strcmp(buff, "exit") == 0) {
                break;
            }
            else {
                cout << "Enter valid choice...." << endl;
            }
        }
    }

    cleanup();
    return 0;
}
