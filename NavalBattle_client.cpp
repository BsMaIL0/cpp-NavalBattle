#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <limits>
#include <conio.h>  // Для _kbhit() и _getch()

#pragma comment(lib, "ws2_32.lib")

const char* SERVER_IP = "127.0.0.1";
const int PORT = 12345;
const int BUFFER_SIZE = 8192;

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

private:
    WSADATA wsaData;
};

// Функция для чтения строки БЕЗ проблем с буфером
std::string readInput() {
    std::string input;

    // Показываем приглашение
    std::cout << ">>> ";

    // Считываем символы до Enter
    while (true) {
        if (_kbhit()) {
            char ch = _getch();

            if (ch == '\r' || ch == '\n') {  // Enter
                std::cout << std::endl;
                break;
            }
            else if (ch == '\b' || ch == 8) {  // Backspace
                if (!input.empty()) {
                    input.pop_back();
                    std::cout << "\b \b";  // Удаляем символ в консоли
                }
            }
            else if (ch >= 32 && ch <= 126) {  // Печатные символы
                input += ch;
                std::cout << ch;
            }
        }
        Sleep(10);  // Небольшая пауза
    }

    return input;
}

int main() {
    setlocale(LC_ALL, "Russian");

    try {
        WSAInitializer wsaInit;

        SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Ошибка создания сокета: " << WSAGetLastError() << "\n";
            return 1;
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(PORT);
        serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

        std::cout << "Подключение к серверу " << SERVER_IP << ":" << PORT << "...\n";

        if (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Ошибка подключения: " << WSAGetLastError() << "\n";
            closesocket(clientSocket);
            return 1;
        }

        std::cout << "Подключено к серверу!\n\n";

        // НЕБЛОКИРУЮЩИЙ РЕЖИМ
        u_long mode = 1;
        ioctlsocket(clientSocket, FIONBIO, &mode);

        char buffer[BUFFER_SIZE];
        bool running = true;

        while (running) {
            // Проверяем данные от сервера
            int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);

            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                std::string message(buffer);
                std::cout << message;

                // Если сервер просит ввод
                if (message.find("Введите команду:") != std::string::npos ||
                    message.find("Введите координаты") != std::string::npos) {

                    // Используем нашу функцию ввода
                    std::string input = readInput();

                    if (!input.empty()) {
                        // Добавляем \n для сервера
                        input += "\n";

                        send(clientSocket, input.c_str(), (int)input.length(), 0);

                        if (input.find("quit") != std::string::npos ||
                            input.find("QUIT") != std::string::npos) {
                            running = false;
                        }
                    }
                }

                // Конец игры
                if (message.find("ПОБЕДА") != std::string::npos ||
                    message.find("ПОРАЖЕНИЕ") != std::string::npos) {
                    running = false;
                }
            }
            else if (bytesReceived == 0) {
                std::cout << "\nСервер отключился\n";
                running = false;
            }
            else if (WSAGetLastError() != WSAEWOULDBLOCK) {
                int err = WSAGetLastError();
                if (err == 10053 || err == 10054) {
                    std::cout << "\nСоединение закрыто\n";
                }
                running = false;
            }

            Sleep(50);
        }

        std::cout << "\nКлиент завершает работу...\n";
        closesocket(clientSocket);
        return 0;

    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
        return 1;
    }
}
