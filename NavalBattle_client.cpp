#define _WINSOCK_DEPRECATED_NO_WARNINGS  // Отключаем предупреждения об устаревших функциях Winsock

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")  // Автоматически линкуем библиотеку Winsock

// Константы для настройки клиента
const char* SERVER_IP = "127.0.0.1";  // IP-адрес сервера (localhost)
const int PORT = 12345;               // Порт сервера
const int BUFFER_SIZE = 4096;         // Размер буфера для приема данных
const int RECV_TIMEOUT_MS = 30000;    // Таймаут приема данных в миллисекундах

// Класс для инициализации и очистки Winsock
class WSAInitializer {
public:
    WSAInitializer() {
        // Инициализируем Winsock
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~WSAInitializer() {
        // Очищаем Winsock при разрушении объекта
        WSACleanup();
    }

    // Запрещаем копирование
    WSAInitializer(const WSAInitializer&) = delete;
    WSAInitializer& operator=(const WSAInitializer&) = delete;

private:
    WSADATA wsaData;  // Структура данных Winsock
};

// Класс для автоматического управления сокетом (RAII)
class SocketRAII {
public:
    SocketRAII(SOCKET s = INVALID_SOCKET) : sock(s) {}
    ~SocketRAII() {
        close();
    }

    // Закрываем сокет
    void close() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    }

    bool isValid() const { return sock != INVALID_SOCKET; }
    operator SOCKET() const { return sock; }

    // Запрещаем копирование
    SocketRAII(const SocketRAII&) = delete;
    SocketRAII& operator=(const SocketRAII&) = delete;

    // Реализуем семантику перемещения
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
    SOCKET sock;  // Дескриптор сокета
};

// Безопасная отправка данных (обрабатывает частичную отправку)
bool safeSend(SOCKET socket, const std::string& data) {
    if (data.empty()) return true;

    int totalSent = 0;
    int dataSize = static_cast<int>(data.size());

    // Отправляем данные пока все не будут отправлены
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

    // Обеспечиваем нуль-терминацию для безопасной работы со строками
    buffer[bytesReceived] = '\0';
    return true;
}

// Проверка формата хода (должен содержать два числа)
bool validateMoveFormat(const std::string& move) {
    std::istringstream iss(move);
    int x, y;

    // Пытаемся извлечь два целых числа
    if (!(iss >> x >> y)) {
        return false;
    }

    // Проверяем что нет лишних данных
    std::string extra;
    if (iss >> extra) {
        return false;
    }

    return true;
}

// Получение и валидация хода от пользователя
std::string getValidatedMove() {
    std::string move;

    while (true) {
        std::cout << "\nEnter your move (x y) or 'quit' to exit: ";
        std::getline(std::cin, move);

        // Убираем пробелы в начале и конце
        move.erase(0, move.find_first_not_of(" \t"));
        move.erase(move.find_last_not_of(" \t") + 1);

        if (move.empty()) {
            std::cout << "Empty input. Please enter coordinates.\n";
            continue;
        }

        // Проверяем команду выхода
        if (move == "quit" || move == "exit") {
            return move;
        }

        // Проверяем формат хода
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
    std::cout << message;  // Выводим сообщение от сервера

    std::string move = getValidatedMove();
    if (move == "quit" || move == "exit") {
        return false;  // Пользователь хочет выйти
    }

    // Добавляем символ новой строки для протокола
    if (move.back() != '\n') {
        move += '\n';
    }

    // Отправляем ход на сервер
    if (!safeSend(clientSocket, move)) {
        return false;
    }

    return true;
}

// Обработка хода противника
bool handleOpponentTurn(SOCKET clientSocket, const std::string& message) {
    std::cout << message;  // Выводим сообщение о ходе противника

    std::vector<char> resultBuf(512);
    int resultReceived;

    // Ждем результат хода от сервера
    if (!safeRecv(clientSocket, resultBuf, resultReceived)) {
        return false;
    }

    std::cout << "Result: " << resultBuf.data();
    return true;
}

int main() {
    try {
        WSAInitializer wsaInit;  // Инициализируем Winsock

        // Создаем сокет
        SocketRAII clientSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!clientSocket.isValid()) {
            std::cerr << "Error creating socket: " << WSAGetLastError() << "\n";
            return 1;
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
        serverAddr.sin_port = htons(PORT);

        // Преобразуем IP-адрес (более безопасная версия)
        if (InetPtonA(AF_INET, SERVER_IP, &serverAddr.sin_addr) != 1) {
            // Fallback на старую функцию для совместимости
            serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
            if (serverAddr.sin_addr.s_addr == INADDR_NONE) {
                std::cerr << "Invalid server IP address\n";
                return 1;
            }
        }

        std::cout << "Connecting to server " << SERVER_IP << ":" << PORT << "...\n";

        // Устанавливаем соединение с сервером
        if (connect(clientSocket, reinterpret_cast<SOCKADDR*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Connect failed: " << WSAGetLastError() << "\n";
            return 1;
        }

        std::cout << "Connected to server successfully.\n";

        // Основной цикл работы клиента
        std::vector<char> buffer(BUFFER_SIZE);
        bool running = true;

        while (running) {
            int bytesReceived;

            // Получаем сообщение от сервера
            if (!safeRecv(clientSocket, buffer, bytesReceived)) {
                break;
            }

            std::string message(buffer.data());

            // Обрабатываем различные типы сообщений от сервера
            if (message.find("YOUR_TURN") != std::string::npos) {
                // Ход текущего игрока
                if (!handleYourTurn(clientSocket, message)) {
                    running = false;
                }
            }
            else if (message.find("OPPONENT_TURN") != std::string::npos) {
                // Ход противника
                if (!handleOpponentTurn(clientSocket, message)) {
                    running = false;
                }
            }
            else if (message.find("GAME_OVER") != std::string::npos) {
                // Конец игры
                std::cout << message;
                running = false;
            }
            else {
                // Другие сообщения (приветствие, состояние игры и т.д.)
                std::cout << message;
            }
        }

        std::cout << "Game client shutting down...\n";
        return 0;

    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    catch (...) {
        std::cerr << "Unknown fatal error occurred\n";
        return 1;
    }
}