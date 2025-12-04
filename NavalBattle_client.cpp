#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define NOMINMAX 
    
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <limits>

#pragma comment(lib, "ws2_32.lib")

// Константы для настройки клиента
const char* DEFAULT_SERVER_IP = "127.0.0.1";
const int DEFAULT_PORT = 12345;
const int BUFFER_SIZE = 4096;
const int RECV_TIMEOUT_MS = 30000;

// Вспомогательные функции для ввода данных
namespace InputUtils {
    // Функция для безопасного получения строки ввода
    std::string getTrimmedInput(const std::string& prompt = "") {
        std::string input;

        while (true) {
            if (!prompt.empty()) {
                std::cout << prompt;
            }

            if (!std::getline(std::cin, input)) {
                if (std::cin.eof()) {
                    std::cout << "\nEOF detected. Exiting...\n";
                    return "quit";
                }
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cout << "Invalid input. Please try again.\n";
                continue;
            }

            // Тримминг строки
            input.erase(std::find_if(input.rbegin(), input.rend(),
                [](unsigned char ch) { return !std::isspace(ch); }).base(), input.end());
            input.erase(input.begin(), std::find_if(input.begin(), input.end(),
                [](unsigned char ch) { return !std::isspace(ch); }));

            return input;
        }
    }

    // Функция для получения IP-адреса от пользователя
    std::string getServerIP() {
        while (true) {
            std::string ip = getTrimmedInput("Enter server IP address [" + std::string(DEFAULT_SERVER_IP) + "]: ");

            if (ip.empty()) {
                return DEFAULT_SERVER_IP;
            }

            // Простая валидация IP-адреса
            sockaddr_in sa;
            if (InetPtonA(AF_INET, ip.c_str(), &(sa.sin_addr)) == 1) {
                return ip;
            }

            // Fallback для старых версий Windows
            unsigned long addr = inet_addr(ip.c_str());
            if (addr != INADDR_NONE && addr != INADDR_ANY) {
                return ip;
            }

            std::cout << "Invalid IP address format. Please enter a valid IPv4 address (e.g., 127.0.0.1)\n";
        }
    }

    // Функция для получения порта от пользователя
    int getServerPort() {
        while (true) {
            std::string portStr = getTrimmedInput("Enter server port [" + std::to_string(DEFAULT_PORT) + "]: ");

            if (portStr.empty()) {
                return DEFAULT_PORT;
            }

            try {
                int port = std::stoi(portStr);
                if (port > 0 && port <= 65535) {
                    return port;
                }
                std::cout << "Port must be between 1 and 65535\n";
            }
            catch (const std::exception&) {
                std::cout << "Invalid port number. Please enter a number between 1 and 65535\n";
            }
        }
    }
}

// Класс для инициализации и очистки Winsock
class WSAInitializer {
public:
    WSAInitializer() {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WSAInitializer() {
        WSACleanup();
    }

    WSAInitializer(const WSAInitializer&) = delete;
    WSAInitializer& operator=(const WSAInitializer&) = delete;

private:
    WSADATA wsaData;
};

// Класс для автоматического управления сокетом (RAII)
class SocketRAII {
public:
    SocketRAII(SOCKET s = INVALID_SOCKET) : sock(s) {}
    ~SocketRAII() { close(); }

    void close() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    }

    bool isValid() const { return sock != INVALID_SOCKET; }
    operator SOCKET() const { return sock; }

    SocketRAII(const SocketRAII&) = delete;
    SocketRAII& operator=(const SocketRAII&) = delete;

    SocketRAII(SocketRAII&& other) noexcept : sock(other.sock) {
        other.sock = INVALID_SOCKET;
    }
    SocketRAII& operator=(SocketRAII&& other) noexcept {
        if (this != &other) {
            close();
            sock = other.sock;
            other.sock = INVALID_SOCKET;
        }
        return *this;
    }

private:
    SOCKET sock;
};

// Безопасная отправка данных
bool safeSend(SOCKET socket, const std::string& data) {
    if (data.empty()) return true;

    int totalSent = 0;
    int dataSize = static_cast<int>(data.size());

    while (totalSent < dataSize) {
        int sent = send(socket, data.c_str() + totalSent, dataSize - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            std::cerr << "Send failed: " << WSAGetLastError() << "\n";
            return false;
        }
        if (sent == 0) {
            std::cerr << "Connection closed during send\n";
            return false;
        }
        totalSent += sent;
    }
    return true;
}

// Безопасный прием данных
bool safeRecv(SOCKET socket, std::vector<char>& buffer, int& bytesReceived) {
    bytesReceived = recv(socket, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);

    if (bytesReceived == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAETIMEDOUT) {
            std::cerr << "Receive timeout\n";
        }
        else {
            std::cerr << "Receive failed: " << error << "\n";
        }
        return false;
    }

    if (bytesReceived == 0) {
        std::cout << "Server disconnected gracefully\n";
        return false;
    }

    buffer[bytesReceived] = '\0';
    return true;
}

// Проверка формата хода
bool validateMoveFormat(const std::string& move) {
    std::istringstream iss(move);
    int x, y;

    if (!(iss >> x >> y)) {
        return false;
    }

    std::string extra;
    if (iss >> extra) {
        return false;
    }

    return true;
}

// Получение и валидация хода от пользователя
std::string getValidatedMove() {
    while (true) {
        std::string move = InputUtils::getTrimmedInput("\nEnter your move (x y) or 'quit' to exit: ");

        if (move.empty()) {
            std::cout << "Empty input. Please enter coordinates.\n";
            continue;
        }

        if (move == "quit" || move == "exit") {
            return move;
        }

        if (validateMoveFormat(move)) {
            return move;
        }
        else {
            std::cout << "Invalid format. Please enter two numbers separated by space (e.g., '1 2').\n";
        }
    }
}

// Обработка хода игрока
bool handleYourTurn(SOCKET clientSocket, const std::string& message) {
    std::cout << message;

    std::string move = getValidatedMove();
    if (move == "quit" || move == "exit") {
        return false;
    }

    if (move.back() != '\n') {
        move += '\n';
    }

    if (!safeSend(clientSocket, move)) {
        return false;
    }

    return true;
}

// Обработка хода противника
bool handleOpponentTurn(SOCKET clientSocket, const std::string& message) {
    std::cout << message;

    std::vector<char> resultBuf(512);
    int resultReceived;

    if (!safeRecv(clientSocket, resultBuf, resultReceived)) {
        return false;
    }

    std::cout << "Result: " << resultBuf.data();
    return true;
}

// Класс для соединения с сервером
class ServerConnector {
public:
    static bool connectToServer(SocketRAII& clientSocket, const std::string& serverIP, int port) {
        // Создаем сокет
        clientSocket = SocketRAII(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!clientSocket.isValid()) {
            std::cerr << "Error creating socket: " << WSAGetLastError() << "\n";
            return false;
        }

        // Устанавливаем таймаут приема данных
        DWORD timeout = RECV_TIMEOUT_MS;
        if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR) {
            std::cerr << "Warning: Failed to set socket timeout: " << WSAGetLastError() << "\n";
        }

        // Настраиваем адрес сервера
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);

        // Пробуем преобразовать IP-адрес с использованием InetPtonA
        if (InetPtonA(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) != 1) {
            // Fallback на старую функцию
            serverAddr.sin_addr.s_addr = inet_addr(serverIP.c_str());
            if (serverAddr.sin_addr.s_addr == INADDR_NONE) {
                std::cerr << "Invalid server IP address: " << serverIP << "\n";
                return false;
            }
        }

        std::cout << "Connecting to server " << serverIP << ":" << port << "...\n";

        // Устанавливаем соединение
        if (connect(clientSocket, reinterpret_cast<SOCKADDR*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Connect failed: " << WSAGetLastError() << "\n";
            std::cerr << "Make sure the server is running on " << serverIP << ":" << port << "\n";
            return false;
        }

        std::cout << "Connected to server successfully.\n";
        return true;
    }
};

int main() {
    try {
        std::cout << "=== Sea Battle Client ===\n\n";

        // Получаем параметры подключения от пользователя
        std::string serverIP = InputUtils::getServerIP();
        int serverPort = InputUtils::getServerPort();

        std::cout << "\nConnecting to " << serverIP << ":" << serverPort << "...\n";

        WSAInitializer wsaInit;

        SocketRAII clientSocket;
        if (!ServerConnector::connectToServer(clientSocket, serverIP, serverPort)) {
            std::cout << "\nFailed to connect to server.\n";
            std::cout << "Possible reasons:\n";
            std::cout << "1. Server is not running\n";
            std::cout << "2. Wrong IP address or port\n";
            std::cout << "3. Firewall blocking the connection\n";
            std::cout << "\nPress Enter to exit...";
            std::cin.ignore();
            return 1;
        }

        // Основной цикл работы клиента
        std::vector<char> buffer(BUFFER_SIZE);
        bool running = true;

        while (running) {
            int bytesReceived;

            if (!safeRecv(clientSocket, buffer, bytesReceived)) {
                break;
            }

            std::string message(buffer.data());

            if (message.find("YOUR_TURN") != std::string::npos) {
                if (!handleYourTurn(clientSocket, message)) {
                    running = false;
                }
            }
            else if (message.find("OPPONENT_TURN") != std::string::npos) {
                if (!handleOpponentTurn(clientSocket, message)) {
                    running = false;
                }
            }
            else if (message.find("GAME_OVER") != std::string::npos) {
                std::cout << message;
                running = false;
            }
            else {
                std::cout << message;
            }
        }

        std::cout << "\nGame client shutting down...\n";
         std::cout << "Press Enter to exit...";
        std::cin.ignore();

        return 0;

    }
    catch (const std::exception& e) {
        std::cerr << "\nFatal error: " << e.what() << "\n";
        std::cerr << "Press Enter to exit...";
        std::cin.ignore();
        return 1;
    }
    catch (...) {
        std::cerr << "\nUnknown fatal error occurred\n";
        std::cerr << "Press Enter to exit...";
        std::cin.ignore();
        return 1;
    }
}