#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define NOMINMAX 

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <functional>
#include <random>
#include <limits>

#pragma comment(lib, "ws2_32.lib")

// Вспомогательные функции для ввода данных
namespace InputUtils {
    std::string getTrimmedInput(const std::string& prompt = "") {
        std::string input;

        while (true) {
            if (!prompt.empty()) {
                std::cout << prompt;
            }

            if (!std::getline(std::cin, input)) {
                if (std::cin.eof()) {
                    std::cout << "\nEOF detected. Exiting...\n";
                    return "";
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

    int getServerPort() {
        const int DEFAULT_PORT = 12345;

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

// Константы игры
const int BOARD_SIZE = 10;
const int SHIP_SIZES[] = { 4, 3, 3, 2, 2, 2, 1, 1, 1, 1 };
const int NUM_SHIPS = 10;
const int BUFFER_SIZE = 256;
const int MAX_PLAYER_NAME = 32;

// Состояния клетки на игровом поле
enum CellState {
    EMPTY = 0,
    SHIP = 1,
    HIT = 2,
    MISS = 3,
    SUNK = 4
};

// Структура корабля
struct Ship {
    int size;
    int hits;
    bool horizontal;
    int x, y;

    bool isSunk() const { return hits >= size; }
};

// Класс игрока
class Player {
public:
    SOCKET socket;
    std::vector<std::vector<CellState>> board;
    std::vector<std::vector<CellState>> enemyView;
    std::vector<Ship> ships;
    bool ready;
    std::string name;
    bool connected;

    Player(SOCKET sock) : socket(sock), ready(false), connected(true) {
        board.resize(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
        enemyView.resize(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
    }

    bool placeShip(int size, int x, int y, bool horizontal) {
        if (horizontal) {
            if (x + size > BOARD_SIZE) return false;
            for (int i = 0; i < size; i++) {
                if (board[y][x + i] != EMPTY) return false;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + i + dx;
                        int ny = y + dy;
                        if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE &&
                            board[ny][nx] == SHIP) return false;
                    }
                }
            }
        }
        else {
            if (y + size > BOARD_SIZE) return false;
            for (int i = 0; i < size; i++) {
                if (board[y + i][x] != EMPTY) return false;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx;
                        int ny = y + i + dy;
                        if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE &&
                            board[ny][nx] == SHIP) return false;
                    }
                }
            }
        }

        Ship ship{ size, 0, horizontal, x, y };
        ships.push_back(ship);

        if (horizontal) {
            for (int i = 0; i < size; i++) {
                board[y][x + i] = SHIP;
            }
        }
        else {
            for (int i = 0; i < size; i++) {
                board[y + i][x] = SHIP;
            }
        }

        return true;
    }

    void autoPlaceShips() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, BOARD_SIZE - 1);
        std::uniform_int_distribution<> dis_bool(0, 1);

        for (int i = 0; i < NUM_SHIPS; i++) {
            int size = SHIP_SIZES[i];
            bool placed = false;
            int attempts = 0;
            const int MAX_ATTEMPTS = 100;

            while (!placed && attempts < MAX_ATTEMPTS) {
                int x = dis(gen);
                int y = dis(gen);
                bool horizontal = dis_bool(gen) == 1;

                placed = placeShip(size, x, y, horizontal);
                attempts++;
            }

            if (!placed) {
                board = std::vector<std::vector<CellState>>(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
                ships.clear();
                i = -1;
            }
        }
    }

    bool allShipsSunk() {
        for (const auto& ship : ships) {
            if (!ship.isSunk()) return false;
        }
        return true;
    }

    void markMissesAroundSunkShip(const Ship& ship, Player* opponent) {
        if (ship.horizontal) {
            for (int i = -1; i <= ship.size; i++) {
                for (int dy = -1; dy <= 1; dy++) {
                    int nx = ship.x + i;
                    int ny = ship.y + dy;

                    if (i >= 0 && i < ship.size && dy == 0) continue;

                    if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                        if (board[ny][nx] == EMPTY) {
                            board[ny][nx] = MISS;
                            opponent->enemyView[ny][nx] = MISS;
                        }
                    }
                }
            }
        }
        else {
            for (int i = -1; i <= ship.size; i++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = ship.x + dx;
                    int ny = ship.y + i;

                    if (i >= 0 && i < ship.size && dx == 0) continue;

                    if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
                        if (board[ny][nx] == EMPTY) {
                            board[ny][nx] = MISS;
                            opponent->enemyView[ny][nx] = MISS;
                        }
                    }
                }
            }
        }
    }

    std::string getBoardString(bool showShips = true) {
        std::string result = "  0 1 2 3 4 5 6 7 8 9\n";
        for (int y = 0; y < BOARD_SIZE; y++) {
            result += std::to_string(y) + ' ';
            for (int x = 0; x < BOARD_SIZE; x++) {
                char symbol = '.';
                if (board[y][x] == HIT) symbol = 'X';
                else if (board[y][x] == MISS) symbol = 'O';
                else if (board[y][x] == SUNK) symbol = '#';
                else if (showShips && board[y][x] == SHIP) symbol = 'S';
                result += symbol;
                result += ' ';
            }
            result += '\n';
        }
        return result;
    }

    std::string getEnemyViewString() {
        std::string result = "  0 1 2 3 4 5 6 7 8 9\n";
        for (int y = 0; y < BOARD_SIZE; y++) {
            result += std::to_string(y) + ' ';
            for (int x = 0; x < BOARD_SIZE; x++) {
                char symbol = '.';
                if (enemyView[y][x] == HIT) symbol = 'X';
                else if (enemyView[y][x] == MISS) symbol = 'O';
                else if (enemyView[y][x] == SUNK) symbol = '#';
                result += symbol;
                result += ' ';
            }
            result += '\n';
        }
        return result;
    }
};

// Безопасные функции для работы с сокетами
bool safeSend(SOCKET socket, const std::string& data) {
    if (socket == INVALID_SOCKET) return false;

    const char* buffer = data.c_str();
    int totalSent = 0;
    int length = (int)data.length();

    while (totalSent < length) {
        int sent = send(socket, buffer + totalSent, length - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                Sleep(10);
                continue;
            }
            return false;
        }
        totalSent += sent;
    }
    return true;
}

bool safeRecv(SOCKET socket, std::string& data, int maxSize = BUFFER_SIZE) {
    if (socket == INVALID_SOCKET) return false;

    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(socket, buffer, maxSize - 1, 0);

    if (bytesReceived == SOCKET_ERROR) {
        return false;
    }
    else if (bytesReceived == 0) {
        return false;
    }

    buffer[bytesReceived] = '\0';
    data = std::string(buffer, bytesReceived);

    data.erase(std::remove(data.begin(), data.end(), '\r'), data.end());
    data.erase(std::remove(data.begin(), data.end(), '\n'), data.end());

    return true;
}

// Класс игры
class Game {
public:
    Player* player1;
    Player* player2;
    bool gameStarted;
    bool gameOver;
    Player* currentPlayer;

    Game(Player* p1, Player* p2) : player1(p1), player2(p2), gameStarted(false), gameOver(false), currentPlayer(p1) {}

    bool bothReady() {
        return player1->ready && player2->ready && player1->connected && player2->connected;
    }

    void switchTurn() {
        currentPlayer = (currentPlayer == player1) ? player2 : player1;
    }

    Player* getOpponent() {
        return (currentPlayer == player1) ? player2 : player1;
    }

    std::string processShot(int x, int y) {
        Player* opponent = getOpponent();

        if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
            return "INVALID: Coordinates out of bounds\n";
        }

        std::string result;
        if (opponent->board[y][x] == SHIP) {
            opponent->board[y][x] = HIT;
            currentPlayer->enemyView[y][x] = HIT;

            for (auto& ship : opponent->ships) {
                bool shipHit = false;
                if (ship.horizontal) {
                    for (int i = 0; i < ship.size; i++) {
                        if (ship.x + i == x && ship.y == y) {
                            ship.hits++;
                            shipHit = true;
                            break;
                        }
                    }
                }
                else {
                    for (int i = 0; i < ship.size; i++) {
                        if (ship.x == x && ship.y + i == y) {
                            ship.hits++;
                            shipHit = true;
                            break;
                        }
                    }
                }

                if (shipHit && ship.isSunk()) {
                    if (ship.horizontal) {
                        for (int i = 0; i < ship.size; i++) {
                            opponent->board[ship.y][ship.x + i] = SUNK;
                            currentPlayer->enemyView[ship.y][ship.x + i] = SUNK;
                        }
                    }
                    else {
                        for (int i = 0; i < ship.size; i++) {
                            opponent->board[ship.y + i][ship.x] = SUNK;
                            currentPlayer->enemyView[ship.y + i][ship.x] = SUNK;
                        }
                    }

                    opponent->markMissesAroundSunkShip(ship, currentPlayer);

                    result = "HIT: Ship sunk!\n";
                    break;
                }
                else if (shipHit) {
                    result = "HIT\n";
                    break;
                }
            }
        }
        else {
            if (opponent->board[y][x] == EMPTY) {
                opponent->board[y][x] = MISS;
                currentPlayer->enemyView[y][x] = MISS;
                result = "MISS\n";
            }
            else {
                result = "MISS: Already attacked this position\n";
            }
        }

        if (opponent->allShipsSunk()) {
            gameOver = true;
        }

        return result;
    }

    bool checkConnections() {
        if (!player1->connected || !player2->connected) {
            gameOver = true;
            return false;
        }
        return true;
    }
};

// Функция для безопасного закрытия сокета
void safeCloseSocket(SOCKET& socket) {
    if (socket != INVALID_SOCKET) {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

int main() {
    std::cout << "=== Sea Battle Server ===\n\n";

    // Получаем порт от пользователя
    int port = InputUtils::getServerPort();

    std::cout << "\nStarting server on port " << port << "...\n";

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Error creating socket: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 2;
    }

    int yes = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR) {
        std::cerr << "Setsockopt failed: " << WSAGetLastError() << "\n";
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 3;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed on port " << port << ": " << WSAGetLastError() << "\n";
        std::cerr << "Make sure the port is not already in use\n";
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 4;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << "\n";
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 5;
    }

    std::cout << "Server is listening on port " << port << "\n";
    std::cout << "Waiting for players to connect...\n";

    // Принятие подключения первого игрока
    SOCKADDR_IN clientAddr1;
    int clientAddrSize = sizeof(clientAddr1);
    SOCKET clientSocket1 = accept(serverSocket, (SOCKADDR*)&clientAddr1, &clientAddrSize);
    if (clientSocket1 == INVALID_SOCKET) {
        std::cerr << "Accept failed for player 1: " << WSAGetLastError() << "\n";
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 6;
    }

    char clientIP1[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr1.sin_addr, clientIP1, INET_ADDRSTRLEN);
    std::cout << "Player 1 connected from " << clientIP1 << ":" << ntohs(clientAddr1.sin_port) << "\n";

    Player player1(clientSocket1);
    player1.name = "Player 1";

    // Принятие подключения второго игрока
    SOCKADDR_IN clientAddr2;
    SOCKET clientSocket2 = accept(serverSocket, (SOCKADDR*)&clientAddr2, &clientAddrSize);
    if (clientSocket2 == INVALID_SOCKET) {
        std::cerr << "Accept failed for player 2: " << WSAGetLastError() << "\n";
        safeCloseSocket(clientSocket1);
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 7;
    }

    char clientIP2[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr2.sin_addr, clientIP2, INET_ADDRSTRLEN);
    std::cout << "Player 2 connected from " << clientIP2 << ":" << ntohs(clientAddr2.sin_port) << "\n";

    Player player2(clientSocket2);
    player2.name = "Player 2";

    Game game(&player1, &player2);

    auto setupPhase = [](Player& player) {
        const std::string welcome = "Welcome to Sea Battle! Placing ships automatically...\n";
        if (!safeSend(player.socket, welcome)) {
            player.connected = false;
            return false;
        }

        player.autoPlaceShips();

        std::string boardMsg = "Your ships have been placed automatically:\n";
        boardMsg += player.getBoardString() + "\n";
        if (!safeSend(player.socket, boardMsg)) {
            player.connected = false;
            return false;
        }

        player.ready = true;
        const std::string readyMsg = "All ships placed! Waiting for other player...\n";
        if (!safeSend(player.socket, readyMsg)) {
            player.connected = false;
            return false;
        }
        return true;
        };

    std::thread setupThread1(setupPhase, std::ref(player1));
    std::thread setupThread2(setupPhase, std::ref(player2));

    setupThread1.join();
    setupThread2.join();

    if (!player1.connected || !player2.connected) {
        std::cerr << "Player disconnected during setup phase\n";
        safeCloseSocket(clientSocket1);
        safeCloseSocket(clientSocket2);
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 8;
    }

    while (!game.bothReady()) {
        Sleep(100);
        if (!game.checkConnections()) {
            std::cerr << "Player disconnected while waiting for readiness\n";
            break;
        }
    }

    if (game.bothReady()) {
        const std::string startMsg = "Game started! Player 1 goes first.\n";
        if (!safeSend(player1.socket, startMsg) || !safeSend(player2.socket, startMsg)) {
            game.gameOver = true;
        }
    }

    std::string inputBuffer;

    while (!game.gameOver && game.checkConnections()) {
        Player* current = game.currentPlayer;
        Player* opponent = game.getOpponent();

        std::string currentTurnMsg = "YOUR_TURN\n";
        currentTurnMsg += "Your board:\n" + current->getBoardString() + "\n";
        currentTurnMsg += "Enemy view:\n" + current->getEnemyViewString() + "\n";
        currentTurnMsg += "Enter coordinates to shoot (x y): ";

        std::string otherTurnMsg = "OPPONENT_TURN\n";
        otherTurnMsg += "Your board:\n" + opponent->getBoardString() + "\n";
        otherTurnMsg += "Enemy view:\n" + opponent->getEnemyViewString() + "\n";
        otherTurnMsg += "Waiting for opponent's move...\n";

        if (!safeSend(current->socket, currentTurnMsg) || !safeSend(opponent->socket, otherTurnMsg)) {
            game.gameOver = true;
            break;
        }

        if (!safeRecv(current->socket, inputBuffer)) {
            std::cout << "Player disconnected\n";
            current->connected = false;
            game.gameOver = true;
            break;
        }

        int x, y;
        char extra;
        if (sscanf_s(inputBuffer.c_str(), "%d %d %c", &x, &y, &extra, 1) == 2) {
            if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
                const std::string errorMsg = "Invalid coordinates. Use values between 0 and 9.\n";
                safeSend(current->socket, errorMsg);
                continue;
            }

            std::string result = game.processShot(x, y);

            std::string resultMsg = current->name + " shot at (" + std::to_string(x) + "," + std::to_string(y) + ") - " + result;

            if (!safeSend(current->socket, resultMsg) || !safeSend(opponent->socket, resultMsg)) {
                game.gameOver = true;
                break;
            }

            if (!game.gameOver && result.find("HIT") == std::string::npos && result.find("Already attacked") == std::string::npos) {
                game.switchTurn();
            }
        }
        else {
            const std::string errorMsg = "Invalid input format. Use: x y (numbers 0-9)\n";
            safeSend(current->socket, errorMsg);
        }
    }

    if (game.gameOver) {
        if (game.bothReady()) {
            std::string winMsg = "Congratulations! You won the game!\n";
            std::string loseMsg = "Game over! You lost.\n";

            safeSend(game.currentPlayer->socket, winMsg);
            safeSend(game.getOpponent()->socket, loseMsg);
        }
        else {
            std::string disconnectMsg = "Game ended due to player disconnect.\n";
            if (player1.connected) safeSend(player1.socket, disconnectMsg);
            if (player2.connected) safeSend(player2.socket, disconnectMsg);
        }
    }

    safeCloseSocket(clientSocket1);
    safeCloseSocket(clientSocket2);
    safeCloseSocket(serverSocket);
    WSACleanup();

    std::cout << "Server shutdown complete\n";
    std::cout << "Press Enter to exit...";
    std::cin.ignore();

    return 0;
}