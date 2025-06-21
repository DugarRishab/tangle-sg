#include "network.h"
#include "tangle.h"
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <arpa/inet.h>
#include <ctime>

using namespace std;

vector<string> knownNodes = {"192.168.29.95"}; // Example nodes
mutex tangleMutex;
const int PORT = 8080;
const int BUFFER_SIZE = 4096;
const int MAX_RETRIES = 1; // Number of times to retry sending data

// Computes SHA-256 checksum of the data
string computeChecksum(const string &data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)data.c_str(), data.size(), hash);

    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// Verifies that the received data has a correct checksum
bool verifyChecksum(const string &data, const string &receivedChecksum)
{
    string calculatedChecksum = computeChecksum(data);
    return calculatedChecksum == receivedChecksum;
}

void printLastTransaction(Tangle &tangle)
{
    Transaction lastTx;
    string latestTimestamp = "0";

    for (const auto &pair : tangle.transactions)
    {
        if (pair.second.timestamp > latestTimestamp)
        {
            latestTimestamp = pair.second.timestamp;
            lastTx = pair.second;
        }
    }

    time_t txTime = static_cast<time_t>(stoll(latestTimestamp));
    time_t currentTime = time(nullptr);
    double elapsedSeconds = difftime(currentTime, txTime);

    // Convert seconds into human-readable format (e.g., minutes/seconds)
    int minutes = static_cast<int>(elapsedSeconds) / 60;
    int seconds = static_cast<int>(elapsedSeconds);

    // Print the last transaction details
    if (!latestTimestamp.empty())
    {
        cout << "[LOG] Last transaction received: ID = " << lastTx.transaction_id
             << ", Sender = " << lastTx.sender << endl
             << ", Receiver = " << lastTx.receiver << endl
             << ", Amount = " << lastTx.amount << " " << lastTx.unit << endl
             << ", Price per unit = " << lastTx.price_per_unit << " " << lastTx.currency << endl
             << ", PoW = " << lastTx.proof_of_work << endl
             << ", Time since creation = " << seconds << " sec ago"
             << endl;
    }
    else
    {
        cout << "[LOG] No transactions found in updated Tangle." << endl;
    }
}

void handleTCPClient(int clientSocket, Tangle &tangle)
{
    char buffer[BUFFER_SIZE] = {0};
    int bytesRead;
    string receivedData;

    while ((bytesRead = read(clientSocket, buffer, BUFFER_SIZE)) > 0)
    {
        receivedData.append(buffer, bytesRead);
    }

    if (!receivedData.empty())
    {
        cout << "[LOG] Received Tangle update" << endl;
        string receivedChecksum = receivedData.substr(receivedData.find_last_of(" ") + 1);
        string actualData = receivedData.substr(0, receivedData.find_last_of(" "));

        if (verifyChecksum(actualData, receivedChecksum))
        {
            tangle.updateFromSerialized(actualData);
            cout << "[LOG] Tangle update verified and applied." << endl;
            printLastTransaction(tangle);
        }
        else
        {
            cerr << "[ERROR] Data corruption detected!" << endl;
        }
    }
    close(clientSocket);
}

void startServer(Tangle &tangle)
{
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        cerr << "[ERROR] Failed to create socket" << endl;
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "[ERROR] Failed to bind socket" << endl;
        close(serverSocket);
        return;
    }

    if (listen(serverSocket, 5) < 0)
    {
        cerr << "[ERROR] Failed to listen on socket" << endl;
        close(serverSocket);
        return;
    }

    cout << "[LOG] Server listening on port " << PORT << endl;

    while (true)
    {
        int clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket >= 0)
        {
            cout << "[LOG] New connection received" << endl;
            thread clientThread(handleTCPClient, clientSocket, ref(tangle));
            clientThread.detach();
        }
    }
}

bool sendOverTCP(string message, string node)
{
    int retryCount = 0;
    while (retryCount < MAX_RETRIES)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1)
        {
            cerr << "[ERROR] Failed to create socket" << endl;
            retryCount++;
            continue;
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(PORT);
        inet_pton(AF_INET, node.c_str(), &serverAddr.sin_addr);

        if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        {
            cerr << "[ERROR] Failed to connect to " << node << " (Attempt " << retryCount + 1 << ")" << endl;
            close(sock);
            retryCount++;
            continue;
        }

        send(sock, message.c_str(), message.size(), 0);
        cout << "[LOG] Sent Tangle update to " << node << "using TCP/IP" << endl;
        close(sock);
        return true;
    }
    return false;
}

void broadcastTangle(const Tangle &tangle)
{
    string tangleData = tangle.serialize();
    string checksum = computeChecksum(tangleData);
    string message = tangleData + " " + checksum;

    for (const auto &node : knownNodes)
    {
        sendOverTCP(message, node);
    }
}