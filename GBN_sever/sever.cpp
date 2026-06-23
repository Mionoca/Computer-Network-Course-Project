#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winsock.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

// 服务端与客户端必须保持一致的协议与网络参数。
#define serverPort 2431
#define clientPort 2427
#define maxDataSize 1048
#define ackLossRate 0.2
#define windowSize 5
#define orderCount 12

// UDP 数据分组解析后的结构。
// 实际发送格式为：两位序号 + "\r\n" + 数据内容。
struct Packet {
    int seq;
    string payload;
};

// 服务端 UDP 套接字状态，以及最近一次完成传输的少量状态记录。
SOCKET serverSocket = INVALID_SOCKET;
sockaddr_in serverAddr{};
sockaddr_in clientAddr{};
int addrLen = sizeof(SOCKADDR);
string lastCompletedProtocol;
int lastCompletedGbnAck = -1;

// 将控制台切换为 UTF-8，避免中文提示乱码。
void configureConsole() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

// 将序号格式化为固定两位，便于 ACK 发送和日志查看。
string formatSeq(int seq) {
    if (seq < 10) {
        return "0" + to_string(seq);
    }
    return to_string(seq);
}

// 将收到的 UDP 文本消息转成字符串，并去掉末尾可能存在的 '\0'。
string trimNullText(const char* data, int size) {
    string text(data, data + size);
    size_t zero = text.find('\0');
    if (zero != string::npos) {
        text.resize(zero);
    }
    return text;
}

// 按给定概率返回 true，仅用于模拟 ACK 丢失。
bool randomEvent(double probability) {
    return (static_cast<double>(rand()) / RAND_MAX) < probability;
}

// 初始化 Winsock，绑定服务端 UDP 端口，并设置客户端地址。
BOOL initSocket() {
    WORD versionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;
    int err = WSAStartup(versionRequested, &wsaData);
    if (err != 0) {
        cout << "加载 winsock 失败，错误代码为: " << WSAGetLastError() << endl;
        return FALSE;
    }

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        cout << "不能找到正确的 winsock 版本" << endl;
        WSACleanup();
        return FALSE;
    }

    serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        cout << "创建服务端套接字失败，错误代码为: " << WSAGetLastError() << endl;
        WSACleanup();
        return FALSE;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    serverAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
    if (bind(serverSocket, reinterpret_cast<SOCKADDR*>(&serverAddr), sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "绑定服务端端口 " << serverPort << " 失败，错误代码为: " << WSAGetLastError() << endl;
        closesocket(serverSocket);
        WSACleanup();
        return FALSE;
    }

    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(clientPort);
    clientAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    u_long nonBlocking = 1;
    ioctlsocket(serverSocket, FIONBIO, &nonBlocking);
    return TRUE;
}

// 程序退出前释放 UDP 套接字和 Winsock 资源。
void cleanupSocket() {
    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }
    WSACleanup();
}

// 将一个 UDP 数据报解析为序号和有效数据内容。
bool parsePacket(const char* data, int size, Packet& packet) {
    if (size < 4) {
        return false;
    }

    int delimiter = -1;
    for (int i = 0; i < size - 1; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            delimiter = i;
            break;
        }
    }

    if (delimiter <= 0) {
        return false;
    }

    string seqText(data, data + delimiter);
    int seq = atoi(seqText.c_str());
    if (seq < 0 || seq >= orderCount) {
        return false;
    }

    packet.seq = seq;
    packet.payload.assign(data + delimiter + 2, data + size);
    return true;
}

// 向客户端发送原始文本响应或 ACK。
bool sendRaw(const string& text) {
    int sent = sendto(
        serverSocket,
        text.data(),
        static_cast<int>(text.size()),
        0,
        reinterpret_cast<SOCKADDR*>(&clientAddr),
        addrLen
    );
    if (sent == SOCKET_ERROR) {
        cout << "发送失败，错误代码为: " << WSAGetLastError() << endl;
        return false;
    }
    return true;
}

// 发送一个 ACK；如果触发模拟 ACK 丢失，则本次不真正发送。
bool sendAckWithLoss(int seq) {
    if (randomEvent(ackLossRate)) {
        cout << "[模拟 ACK 丢失] seq=" << formatSeq(seq) << endl;
        return false;
    }

    string ack = formatSeq(seq);
    if (!sendRaw(ack)) {
        return false;
    }

    cout << "[ACK] 已发送 seq=" << ack << endl;
    return true;
}

// 接收短文本消息，例如客户端命令或分组总数。
bool receiveText(string& text, int timeoutMs) {
    DWORD deadline = GetTickCount() + timeoutMs;
    char buffer[maxDataSize]{};
    while (GetTickCount() < deadline) {
        int recvSize = recvfrom(
            serverSocket,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<SOCKADDR*>(&clientAddr),
            &addrLen
        );
        if (recvSize > 0) {
            text = trimNullText(buffer, recvSize);
            return true;
        }
        Sleep(50);
    }
    return false;
}

// 接收并解析一个数据分组。
bool receivePacket(Packet& packet, int timeoutMs) {
    DWORD deadline = GetTickCount() + timeoutMs;
    char buffer[maxDataSize]{};
    while (GetTickCount() < deadline) {
        int recvSize = recvfrom(
            serverSocket,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<SOCKADDR*>(&clientAddr),
            &addrLen
        );
        if (recvSize > 0) {
            return parsePacket(buffer, recvSize, packet);
        }
        Sleep(20);
    }
    return false;
}

// 服务端回复 "Ok" 后，客户端发送的第一条消息是分组总数。
int receivePacketCount() {
    string text;
    if (!receiveText(text, 5000)) {
        return -1;
    }
    return atoi(text.c_str());
}

// Go-Back-N 接收端逻辑：
// - 只接收当前期望序号的分组；
// - 丢弃乱序分组；
// - 发送累计 ACK，ACK 值表示下一个期望序号。
bool receiveGBN() {
    ofstream file("data.txt", ios::out | ios::binary | ios::trunc);
    if (!file.is_open()) {
        cout << "[GBN] 无法打开 data.txt 写入。" << endl;
        return false;
    }

    sendRaw("Ok");
    int remaining = receivePacketCount();
    if (remaining < 0) {
        cout << "[GBN] 未收到分组总数。" << endl;
        return false;
    }

    cout << "\n[GBN] 准备接收 " << remaining << " 个分组。" << endl;
    int expectedSeq = 0;
    DWORD lastActive = GetTickCount();

    while (remaining > 0) {
        // 如果客户端长时间没有有效数据，则终止本次接收。
        if (GetTickCount() - lastActive > 30000) {
            cout << "[GBN] 30 秒无有效数据，接收终止。" << endl;
            return false;
        }

        Packet packet;
        if (!receivePacket(packet, 500)) {
            continue;
        }

        lastActive = GetTickCount();
        if (packet.seq == expectedSeq) {
            // 按序到达的分组直接写入 data.txt，并推进期望序号。
            file.write(packet.payload.data(), static_cast<streamsize>(packet.payload.size()));
            remaining--;
            expectedSeq = (expectedSeq + 1) % orderCount;
            cout << "[GBN] 收到期望分组 seq=" << formatSeq(packet.seq)
                 << "，剩余 " << remaining << endl;
        }
        else {
            // GBN 协议要求接收端忽略乱序分组。
            cout << "[GBN] 丢弃乱序分组 seq=" << formatSeq(packet.seq)
                 << "，当前期望 seq=" << formatSeq(expectedSeq) << endl;
        }

        sendAckWithLoss(expectedSeq);
    }

    cout << "[GBN] 接收完成，已写入 data.txt。" << endl;
    lastCompletedProtocol = "gbn";
    lastCompletedGbnAck = expectedSeq;
    return true;
}

// 将序号映射为当前 SR 窗口内的绝对分组下标。
int findAbsoluteIndexForSeq(int seq, int base, int total) {
    int end = min(total, base + windowSize);
    for (int i = base; i < end; ++i) {
        if (i % orderCount == seq) {
            return i;
        }
    }
    return -1;
}

// Selective Repeat 接收端逻辑：
// - 接收当前窗口内的任意分组；
// - 缓存乱序到达的分组；
// - 只有窗口起点按序推进时，才把缓存数据写入文件。
bool receiveSR() {
    ofstream file("data.txt", ios::out | ios::binary | ios::trunc);
    if (!file.is_open()) {
        cout << "[SR] 无法打开 data.txt 写入。" << endl;
        return false;
    }

    sendRaw("Ok");
    int total = receivePacketCount();
    if (total < 0) {
        cout << "[SR] 未收到分组总数。" << endl;
        return false;
    }

    cout << "\n[SR] 准备接收 " << total << " 个分组。" << endl;
    vector<bool> received(total, false);
    vector<string> buffer(total);
    int base = 0;
    DWORD lastActive = GetTickCount();

    while (base < total) {
        // 如果客户端长时间没有有效数据，则终止本次接收。
        if (GetTickCount() - lastActive > 30000) {
            cout << "[SR] 30 秒无有效数据，接收终止。" << endl;
            return false;
        }

        Packet packet;
        if (!receivePacket(packet, 500)) {
            continue;
        }

        lastActive = GetTickCount();
        int index = findAbsoluteIndexForSeq(packet.seq, base, total);
        if (index >= 0) {
            if (!received[index]) {
                // 先缓存该分组，等待更早的分组到齐后再按序写入。
                received[index] = true;
                buffer[index] = packet.payload;
                cout << "[SR] 缓存分组#" << index
                     << " seq=" << formatSeq(packet.seq)
                     << "，窗口 " << base << "-" << (min(total, base + windowSize) - 1) << endl;
            }
            else {
                cout << "[SR] 收到重复分组#" << index
                     << " seq=" << formatSeq(packet.seq) << endl;
            }

            sendAckWithLoss(packet.seq);

            int oldBase = base;
            while (base < total && received[base]) {
                // 将从窗口起点开始连续到达的缓存分组按顺序写入文件。
                file.write(buffer[base].data(), static_cast<streamsize>(buffer[base].size()));
                buffer[base].clear();
                base++;
            }
            if (oldBase != base) {
                cout << "[SR] 窗口滑动 " << oldBase << " -> " << base
                     << "，进度 " << base << "/" << total << endl;
            }
        }
        else {
            cout << "[SR] 分组 seq=" << formatSeq(packet.seq)
                 << " 不在当前窗口，丢弃。" << endl;
            sendAckWithLoss(packet.seq);
        }
    }

    cout << "[SR] 接收完成，已写入 data.txt。" << endl;
    lastCompletedProtocol = "sr";
    return true;
}

// 服务端启动时显示的提示信息。
void printBanner() {
    cout << "================ UDP 可靠文件传输服务端 ================" << endl;
    cout << "监听地址: 127.0.0.1:" << serverPort << "    客户端端口: " << clientPort << endl;
    cout << "输出文件: data.txt" << endl;
    cout << "等待客户端指令: -testgbn / -testsr / -time / -quit" << endl;
    cout << "--------------------------------------------------------" << endl;
}

// 服务端主命令循环；文件传输命令会阻塞直到本次传输结束。
void serve() {
    printBanner();
    char buffer[maxDataSize]{};
    while (true) {
        int recvSize = recvfrom(
            serverSocket,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<SOCKADDR*>(&clientAddr),
            &addrLen
        );

        if (recvSize <= 0) {
            Sleep(100);
            continue;
        }

        string command = trimNullText(buffer, recvSize);
        cout << "\n[指令] " << command << endl;

        if (command == "-time") {
            SYSTEMTIME st;
            GetLocalTime(&st);
            string timeText = to_string(st.wYear) + "-" + to_string(st.wMonth) + "-" + to_string(st.wDay) + " " +
                to_string(st.wHour) + ":" + to_string(st.wMinute) + ":" + to_string(st.wSecond);
            sendRaw(timeText);
        }
        else if (command == "-quit") {
            sendRaw("goodbye");
        }
        else if (command == "-testgbn") {
            receiveGBN();
        }
        else if (command == "-testsr") {
            receiveSR();
        }
        else {
            Packet duplicatePacket;
            if (parsePacket(buffer, recvSize, duplicatePacket) && !lastCompletedProtocol.empty()) {
                // 如果最后一个 ACK 丢失，客户端可能在服务端已经完成传输后继续重传。
                // 此时补发 ACK，而不是把重复数据分组误认为新的用户命令。
                if (lastCompletedProtocol == "gbn" && lastCompletedGbnAck >= 0) {
                    cout << "[补发 ACK] 已完成 GBN，重复分组 seq="
                         << formatSeq(duplicatePacket.seq)
                         << "，补发累计 ACK seq=" << formatSeq(lastCompletedGbnAck) << endl;
                    sendAckWithLoss(lastCompletedGbnAck);
                }
                else if (lastCompletedProtocol == "sr") {
                    cout << "[补发 ACK] 已完成 SR，重复分组 seq="
                         << formatSeq(duplicatePacket.seq) << endl;
                    sendAckWithLoss(duplicatePacket.seq);
                }
            }
            else {
                sendRaw("未知指令，请使用 -testgbn / -testsr / -time / -quit");
            }
        }
    }
}

int main() {
    configureConsole();
    srand(static_cast<unsigned int>(time(nullptr)) + 1);
    if (!initSocket()) {
        return 1;
    }

    serve();
    cleanupSocket();
    return 0;
}
