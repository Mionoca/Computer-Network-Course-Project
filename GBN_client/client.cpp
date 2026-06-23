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

// 客户端和服务端共同使用的协议与网络参数。
#define windowSize 5
#define clientPort 2427
#define serverPort 2431
#define maxDataSize 1048
#define maxPayloadSize 1024
#define TimerCount 2
#define orderCount 12
#define sendLossRate 0.3
#define canRepeat 20

// 文件分片后的一个 UDP 数据分组。
// 实际发送格式为：两位序号 + "\r\n" + 数据内容。
struct Packet {
    int seq;
    string payload;
};

// 客户端发送和接收函数共用的 UDP 套接字状态。
SOCKET clientSocket = INVALID_SOCKET;
sockaddr_in serverAddr{};
int addrLen = sizeof(SOCKADDR);

// 将控制台切换为 UTF-8，避免中文提示乱码。
void configureConsole() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

// 将序号格式化为固定两位，便于解析分组和查看日志。
string formatSeq(int seq) {
    if (seq < 10) {
        return "0" + to_string(seq);
    }
    return to_string(seq);
}

// 将收到的 UDP 文本响应转成字符串，并去掉末尾可能存在的 '\0'。
string trimNullText(const char* data, int size) {
    string text(data, data + size);
    size_t zero = text.find('\0');
    if (zero != string::npos) {
        text.resize(zero);
    }
    return text;
}

// 按给定概率返回 true，仅用于模拟数据包丢失。
bool randomEvent(double probability) {
    return (static_cast<double>(rand()) / RAND_MAX) < probability;
}

// 初始化 Winsock，绑定客户端 UDP 端口，并设置服务端地址。
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

    clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSocket == INVALID_SOCKET) {
        cout << "创建客户端套接字失败，错误代码为: " << WSAGetLastError() << endl;
        WSACleanup();
        return FALSE;
    }

    sockaddr_in clientAddr{};
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(clientPort);
    clientAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
    if (bind(clientSocket, reinterpret_cast<SOCKADDR*>(&clientAddr), sizeof(SOCKADDR)) == SOCKET_ERROR) {
        cout << "绑定客户端端口 " << clientPort << " 失败，错误代码为: " << WSAGetLastError() << endl;
        closesocket(clientSocket);
        WSACleanup();
        return FALSE;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    u_long nonBlocking = 1;
    ioctlsocket(clientSocket, FIONBIO, &nonBlocking);
    return TRUE;
}

// 程序退出前释放 UDP 套接字和 Winsock 资源。
void cleanupSocket() {
    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }
    WSACleanup();
}

// 以二进制方式读取 test.txt，并按最大载荷长度切分为多个分组。
// 分组序号在 [0, orderCount) 范围内循环使用。
vector<Packet> readFilePackets(const string& path) {
    vector<Packet> packets;
    ifstream file(path.c_str(), ios::in | ios::binary);
    if (!file.is_open()) {
        cout << "无法打开文件 " << path << "，请确认文件存在。" << endl;
        return packets;
    }

    int seq = 0;
    vector<char> buffer(maxPayloadSize);
    while (file) {
        file.read(buffer.data(), buffer.size());
        streamsize bytesRead = file.gcount();
        if (bytesRead <= 0) {
            break;
        }

        Packet packet;
        packet.seq = seq;
        packet.payload.assign(buffer.data(), static_cast<size_t>(bytesRead));
        packets.push_back(packet);
        seq = (seq + 1) % orderCount;
    }

    return packets;
}

// 组装通过 UDP 发送的完整字节内容。
string buildPacket(const Packet& packet) {
    return formatSeq(packet.seq) + "\r\n" + packet.payload;
}

// 发送原始字节到服务端，不额外添加协议头。
bool sendRaw(const string& data) {
    int sent = sendto(
        clientSocket,
        data.data(),
        static_cast<int>(data.size()),
        0,
        reinterpret_cast<SOCKADDR*>(&serverAddr),
        sizeof(serverAddr)
    );
    if (sent == SOCKET_ERROR) {
        cout << "发送失败，错误代码为: " << WSAGetLastError() << endl;
        return false;
    }
    return true;
}

// 发送一个协议分组；如果触发模拟丢包，则本次不真正发送。
bool sendPacketWithLoss(const Packet& packet, int absoluteIndex, bool retransmit) {
    if (randomEvent(sendLossRate)) {
        cout << "[模拟丢包] " << (retransmit ? "重传" : "发送")
             << " 分组#" << absoluteIndex << " seq=" << formatSeq(packet.seq) << endl;
        return false;
    }

    string raw = buildPacket(packet);
    if (!sendRaw(raw)) {
        return false;
    }

    cout << "[发送] " << (retransmit ? "重传" : "新包")
         << " 分组#" << absoluteIndex << " seq=" << formatSeq(packet.seq)
         << " bytes=" << packet.payload.size() << endl;
    return true;
}

// 从服务端接收一个 ACK 序号。
bool receiveAck(int& ackSeq) {
    char ackBuffer[64]{};
    int recvSize = recvfrom(
        clientSocket,
        ackBuffer,
        sizeof(ackBuffer),
        0,
        reinterpret_cast<SOCKADDR*>(&serverAddr),
        &addrLen
    );

    if (recvSize <= 0) {
        return false;
    }

    string ackText = trimNullText(ackBuffer, recvSize);
    if (ackText.empty()) {
        return false;
    }

    ackSeq = atoi(ackText.c_str());
    return ackSeq >= 0 && ackSeq < orderCount;
}

// 显示新命令菜单前，清理套接字里残留的旧数据报。
void drainSocket(int milliseconds) {
    DWORD endTime = GetTickCount() + milliseconds;
    char buffer[maxDataSize]{};
    while (GetTickCount() < endTime) {
        int recvSize = recvfrom(
            clientSocket,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<SOCKADDR*>(&serverAddr),
            &addrLen
        );
        if (recvSize <= 0) {
            Sleep(20);
        }
    }
}

// 等待服务端返回短文本响应，例如 "Ok"、"goodbye" 或服务端时间。
bool waitForTextResponse(string& response, int timeoutMs) {
    DWORD deadline = GetTickCount() + timeoutMs;
    char buffer[maxDataSize]{};
    while (GetTickCount() < deadline) {
        int recvSize = recvfrom(
            clientSocket,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<SOCKADDR*>(&serverAddr),
            &addrLen
        );
        if (recvSize > 0) {
            response = trimNullText(buffer, recvSize);
            return true;
        }
        Sleep(50);
    }
    return false;
}

// 告诉服务端本次文件传输一共有多少个数据分组。
bool sendPacketCount(int count) {
    string countText = to_string(count);
    return sendRaw(countText);
}

// Go-Back-N 发送端逻辑：
// - 不断填充发送窗口；
// - 将 ACK 理解为接收方下一个期望收到的序号；
// - 超时后重传当前窗口内所有未确认分组。
bool sendMessageGBN(const vector<Packet>& packets) {
    cout << "\n[GBN] 准备发送 " << packets.size()
         << " 个分组，窗口大小=" << windowSize
         << "，序号空间=" << orderCount << endl;

    if (!sendPacketCount(static_cast<int>(packets.size()))) {
        return false;
    }

    if (packets.empty()) {
        cout << "[GBN] 文件为空，无需发送数据分组。" << endl;
        return true;
    }

    int base = 0;
    int nextToSend = 0;
    int timeoutCount = 0;
    DWORD lastSendTime = GetTickCount();

    while (base < static_cast<int>(packets.size())) {
        // 向当前 GBN 窗口填充尚未发送的新分组。
        while (nextToSend < static_cast<int>(packets.size()) && nextToSend < base + windowSize) {
            sendPacketWithLoss(packets[nextToSend], nextToSend, false);
            nextToSend++;
            lastSendTime = GetTickCount();
        }

        int ackSeq = -1;
        if (receiveAck(ackSeq)) {
            // GBN 的 ACK 表示 ackSeq 之前的分组都已经按序到达。
            int distance = (ackSeq - packets[base].seq + orderCount) % orderCount;
            int outstanding = nextToSend - base;

            if (distance > 0 && distance <= outstanding) {
                int oldBase = base;
                base += distance;
                timeoutCount = 0;
                lastSendTime = GetTickCount();
                cout << "[ACK] GBN 累计确认到 seq=" << formatSeq(ackSeq)
                     << "，窗口 " << oldBase << " -> " << base
                     << "，进度 " << base << "/" << packets.size() << endl;
            }
            else {
                cout << "[ACK] 收到重复或过期 ACK seq=" << formatSeq(ackSeq) << endl;
            }
        }

        if (base < nextToSend && GetTickCount() - lastSendTime >= TimerCount * 1000) {
            // GBN 超时后会重传当前窗口内所有未确认分组。
            timeoutCount++;
            if (timeoutCount > canRepeat) {
                cout << "[GBN] 超过最大重传次数，传输失败。" << endl;
                return false;
            }

            cout << "[GBN] 超时，第 " << timeoutCount << " 次重传窗口内分组 "
                 << base << " 到 " << (nextToSend - 1) << endl;
            for (int i = base; i < nextToSend; ++i) {
                sendPacketWithLoss(packets[i], i, true);
            }
            lastSendTime = GetTickCount();
        }

        Sleep(20);
    }

    cout << "[GBN] 文件传输完成。" << endl;
    return true;
}

// Selective Repeat 发送端逻辑：
// - 记录每个绝对分组下标是否已经确认；
// - 只有窗口起点开始的连续分组都确认后才滑动窗口；
// - 超时后只重传当前窗口中未确认的分组。
bool sendMessageSR(const vector<Packet>& packets) {
    cout << "\n[SR] 准备发送 " << packets.size()
         << " 个分组，窗口大小=" << windowSize
         << "，序号空间=" << orderCount << endl;

    if (!sendPacketCount(static_cast<int>(packets.size()))) {
        return false;
    }

    if (packets.empty()) {
        cout << "[SR] 文件为空，无需发送数据分组。" << endl;
        return true;
    }

    vector<bool> acked(packets.size(), false);
    int base = 0;
    int nextToSend = 0;
    int timeoutCount = 0;
    DWORD lastSendTime = GetTickCount();

    while (base < static_cast<int>(packets.size())) {
        // 向当前 SR 窗口填充新分组。
        while (nextToSend < static_cast<int>(packets.size()) && nextToSend < base + windowSize) {
            sendPacketWithLoss(packets[nextToSend], nextToSend, false);
            nextToSend++;
            lastSendTime = GetTickCount();
        }

        int ackSeq = -1;
        if (receiveAck(ackSeq)) {
            // SR 的 ACK 只确认单个分组，不代表前缀全部到达。
            bool matched = false;
            for (int i = base; i < nextToSend; ++i) {
                if (!acked[i] && packets[i].seq == ackSeq) {
                    acked[i] = true;
                    matched = true;
                    timeoutCount = 0;
                    cout << "[ACK] SR 确认分组#" << i
                         << " seq=" << formatSeq(ackSeq) << endl;
                    break;
                }
            }

            if (!matched) {
                cout << "[ACK] 收到重复或过期 ACK seq=" << formatSeq(ackSeq) << endl;
            }

            int oldBase = base;
            while (base < static_cast<int>(packets.size()) && acked[base]) {
                base++;
            }
            if (base != oldBase) {
                cout << "[SR] 窗口 " << oldBase << " -> " << base
                     << "，进度 " << base << "/" << packets.size() << endl;
            }

            lastSendTime = GetTickCount();
        }

        if (base < nextToSend && GetTickCount() - lastSendTime >= TimerCount * 1000) {
            // SR 超时后只重传当前窗口中尚未确认的分组。
            timeoutCount++;
            if (timeoutCount > canRepeat) {
                cout << "[SR] 超过最大重传次数，传输失败。" << endl;
                return false;
            }

            cout << "[SR] 超时，第 " << timeoutCount << " 次重传窗口内未确认分组。" << endl;
            for (int i = base; i < nextToSend; ++i) {
                if (!acked[i]) {
                    sendPacketWithLoss(packets[i], i, true);
                }
            }
            lastSendTime = GetTickCount();
        }

        Sleep(20);
    }

    cout << "[SR] 文件传输完成。" << endl;
    return true;
}

// 发送用户命令，等待服务端确认后，执行 GBN 或 SR 传输。
bool runTransfer(const string& command) {
    string response;
    if (!waitForTextResponse(response, 5000) || response != "Ok") {
        cout << "服务端未确认传输请求，请确认服务端已启动。" << endl;
        return false;
    }

    vector<Packet> packets = readFilePackets("test.txt");
    if (packets.empty()) {
        cout << "没有可发送的数据，传输取消。" << endl;
        return false;
    }

    if (command == "-testgbn") {
        return sendMessageGBN(packets);
    }
    return sendMessageSR(packets);
}

// 每次输入命令前显示的控制台菜单。
void printMenu() {
    cout << "\n================ UDP 可靠文件传输客户端 ================\n";
    cout << "本地端口: " << clientPort << "    服务端: 127.0.0.1:" << serverPort << "\n";
    cout << "输入文件: test.txt\n";
    cout << "--------------------------------------------------------\n";
    cout << "1. -testgbn   使用 Go-Back-N 传输文件\n";
    cout << "2. -testsr    使用 Selective Repeat 传输文件\n";
    cout << "3. -time      获取服务端当前时间\n";
    cout << "4. -quit      退出程序\n";
    cout << "请输入指令: ";
}

int main() {
    configureConsole();
    srand(static_cast<unsigned int>(time(nullptr)));

    if (!initSocket()) {
        return 1;
    }

    cout << "客户端已启动。" << endl;
    string input;
    while (true) {
        drainSocket(200);
        printMenu();
        if (!(cin >> input)) {
            break;
        }

        string command = input;
        command.push_back('\0');
        if (!sendRaw(command)) {
            break;
        }

        if (input == "-testgbn" || input == "-testsr") {
            bool ok = runTransfer(input);
            cout << (ok ? "传输结果: 成功" : "传输结果: 失败") << endl;
        }
        else {
            string response;
            if (waitForTextResponse(response, 5000)) {
                cout << "服务端响应: " << response << endl;
            }
            else {
                cout << "服务端无响应。" << endl;
            }

            if (input == "-quit") {
                break;
            }
        }
    }

    cleanupSocket();
    return 0;
}
