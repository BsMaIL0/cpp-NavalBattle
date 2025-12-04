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
#include <queue>
#include <map>
#include <atomic>
#include <chrono>
#include <mutex>

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

bool safeSend(SOCKET socket, const std::string& data);
void safeCloseSocket(SOCKET& socket);
bool safeRecv(SOCKET socket, std::string& data, int maxSize);

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
    int playerId;
    sockaddr_in clientAddr;

    Player(SOCKET sock, const sockaddr_in& addr, int id)
        : socket(sock), ready(false), connected(true), playerId(id), clientAddr(addr) {
        board.resize(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
        enemyView.resize(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
        name = "Player " + std::to_string(id);
    }

    ~Player() {
        disconnect();
    }

    void disconnect() {
        if (connected) {
            connected = false;
            safeCloseSocket(socket);
        }
    }

    std::string getIPAddress() const {
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
        return std::string(ipStr);
    }

    int getPort() const {
        return ntohs(clientAddr.sin_port);
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

// Класс игры
class Game {
public:
    Player* player1;
    Player* player2;
    bool gameStarted;
    bool gameOver;
    Player* currentPlayer;
    std::atomic<bool> active;

    Game(Player* p1, Player* p2)
        : player1(p1), player2(p2), gameStarted(false), gameOver(false),
        currentPlayer(p1), active(true) {
    }

    ~Game() {
        endGame("Game terminated");
    }

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

    void endGame(const std::string& reason) {
        if (!active) return;

        active = false;
        gameOver = true;

        if (player1->connected) {
            safeSend(player1->socket, "GAME_OVER: " + reason + "\n");
        }
        if (player2->connected) {
            safeSend(player2->socket, "GAME_OVER: " + reason + "\n");
        }
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
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
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
        int error = WSAGetLastError();
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

// Функция для безопасного закрытия сокета
void safeCloseSocket(SOCKET& socket) {
    if (socket != INVALID_SOCKET) {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

// Класс для управления сервером
class GameServer {
private:
    SOCKET serverSocket;
    int port;
    std::atomic<bool> running;
    std::queue<Player*> waitingPlayers;
    std::vector<Game*> activeGames;
    std::vector<std::thread> gameThreads;
    std::mutex queueMutex;
    std::mutex gamesMutex;
    int nextPlayerId;

public:
    GameServer(int serverPort) : port(serverPort), running(false), nextPlayerId(1) {
        serverSocket = INVALID_SOCKET;
    }

    ~GameServer() {
        stop();
    }

    bool initialize() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed\n";
            return false;
        }

        serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "Error creating socket: " << WSAGetLastError() << "\n";
            WSACleanup();
            return false;
        }

        int yes = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR) {
            std::cerr << "Setsockopt failed: " << WSAGetLastError() << "\n";
            safeCloseSocket(serverSocket);
            WSACleanup();
            return false;
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);

        if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed on port " << port << ": " << WSAGetLastError() << "\n";
            safeCloseSocket(serverSocket);
            WSACleanup();
            return false;
        }

        if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "Listen failed: " << WSAGetLastError() << "\n";
            safeCloseSocket(serverSocket);
            WSACleanup();
            return false;
        }

        std::cout << "Server initialized on port " << port << "\n";
        return true;
    }

    void start() {
        running = true;
        std::cout << "Server started. Waiting for players...\n";

        // Поток для приема новых подключений
        std::thread acceptorThread(&GameServer::acceptConnections, this);

        // Поток для управления играми
        std::thread matchmakerThread(&GameServer::matchmakingLoop, this);

        // Поток для очистки завершенных игр
        std::thread cleanupThread(&GameServer::cleanupLoop, this);

        // Основной поток для управления сервером
        serverManagementLoop();

        acceptorThread.join();
        matchmakerThread.join();
        cleanupThread.join();
    }

    void stop() {
        running = false;

        // Закрыть все активные игры
        {
            std::lock_guard<std::mutex> lock(gamesMutex);
            for (auto game : activeGames) {
                game->endGame("Server shutdown");
                delete game;
            }
            activeGames.clear();
        }

        // Очистить очередь ожидания
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            while (!waitingPlayers.empty()) {
                Player* player = waitingPlayers.front();
                waitingPlayers.pop();
                safeSend(player->socket, "Server is shutting down. Goodbye!\n");
                delete player;
            }
        }

        safeCloseSocket(serverSocket);
        WSACleanup();

        std::cout << "Server stopped.\n";
    }

private:
    void acceptConnections() {
        fd_set readSet;
        timeval timeout;

        while (running) {
            FD_ZERO(&readSet);
            FD_SET(serverSocket, &readSet);

            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

            if (selectResult > 0 && FD_ISSET(serverSocket, &readSet)) {
                sockaddr_in clientAddr;
                int clientAddrSize = sizeof(clientAddr);
                SOCKET clientSocket = accept(serverSocket, (SOCKADDR*)&clientAddr, &clientAddrSize);

                if (clientSocket != INVALID_SOCKET) {
                    // Устанавливаем таймаут на чтение
                    DWORD timeoutMs = 30000;
                    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO,
                        (char*)&timeoutMs, sizeof(timeoutMs));

                    char clientIP[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

                    std::cout << "New connection from " << clientIP << ":"
                        << ntohs(clientAddr.sin_port) << std::endl;

                    Player* newPlayer = new Player(clientSocket, clientAddr, nextPlayerId++);

                    // Отправляем приветственное сообщение
                    std::string welcomeMsg = "Welcome to Sea Battle Server!\n";
                    welcomeMsg += "You are Player " + std::to_string(newPlayer->playerId) + "\n";
                    welcomeMsg += "Waiting for opponent...\n";
                    safeSend(clientSocket, welcomeMsg);

                    // Добавляем игрока в очередь ожидания
                    {
                        std::lock_guard<std::mutex> lock(queueMutex);
                        waitingPlayers.push(newPlayer);
                    }
                }
            }
            else if (selectResult == SOCKET_ERROR) {
                if (running) {
                    std::cerr << "Select error in accept thread: " << WSAGetLastError() << std::endl;
                }
                break;
            }
        }
    }

    void matchmakingLoop() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Проверяем, есть ли как минимум 2 игрока в очереди
            std::lock_guard<std::mutex> lock(queueMutex);
            if (waitingPlayers.size() >= 2) {
                Player* player1 = waitingPlayers.front();
                waitingPlayers.pop();
                Player* player2 = waitingPlayers.front();
                waitingPlayers.pop();

                // Проверяем, что оба игрока еще подключены
                if (player1->connected && player2->connected) {
                    // Создаем новую игру в отдельном потоке
                    Game* newGame = new Game(player1, player2);
                    {
                        std::lock_guard<std::mutex> lock(gamesMutex);
                        activeGames.push_back(newGame);
                    }

                    std::thread gameThread(&GameServer::runGame, this, newGame);
                    gameThread.detach();

                    std::cout << "Started new game between Player " << player1->playerId
                        << " and Player " << player2->playerId << std::endl;
                }
                else {
                    // Если кто-то отключился, удаляем обоих
                    if (player1->connected) {
                        safeSend(player1->socket, "Opponent disconnected during matchmaking\n");
                        delete player1;
                    }
                    if (player2->connected) {
                        safeSend(player2->socket, "Opponent disconnected during matchmaking\n");
                        delete player2;
                    }
                }
            }
        }
    }

    void runGame(Game* game) {
        auto setupPhase = [](Player& player) -> bool {
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

        // Фаза расстановки кораблей
        std::thread setupThread1(setupPhase, std::ref(*game->player1));
        std::thread setupThread2(setupPhase, std::ref(*game->player2));

        setupThread1.join();
        setupThread2.join();

        if (!game->player1->connected || !game->player2->connected) {
            game->endGame("Player disconnected during setup");
            return;
        }

        // Ждем, пока оба игрока будут готовы
        while (!game->bothReady() && game->active && running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!game->checkConnections()) {
                game->endGame("Player disconnected while waiting for readiness");
                return;
            }
        }

        if (game->bothReady() && game->active && running) {
            const std::string startMsg = "Game started! Player 1 goes first.\n";
            if (!safeSend(game->player1->socket, startMsg) || !safeSend(game->player2->socket, startMsg)) {
                game->endGame("Failed to send start message");
                return;
            }

            game->gameStarted = true;
        }
        else {
            return;
        }

        // Основной игровой цикл
        std::string inputBuffer;
        while (!game->gameOver && game->active && running && game->checkConnections()) {
            Player* current = game->currentPlayer;
            Player* opponent = game->getOpponent();

            std::string currentTurnMsg = "YOUR_TURN\n";
            currentTurnMsg += "Your board:\n" + current->getBoardString() + "\n";
            currentTurnMsg += "Enemy view:\n" + current->getEnemyViewString() + "\n";
            currentTurnMsg += "Enter coordinates to shoot (x y): ";

            std::string otherTurnMsg = "OPPONENT_TURN\n";
            otherTurnMsg += "Your board:\n" + opponent->getBoardString() + "\n";
            otherTurnMsg += "Enemy view:\n" + opponent->getEnemyViewString() + "\n";
            otherTurnMsg += "Waiting for opponent's move...\n";

            if (!safeSend(current->socket, currentTurnMsg) || !safeSend(opponent->socket, otherTurnMsg)) {
                game->endGame("Failed to send turn message");
                break;
            }

            if (!safeRecv(current->socket, inputBuffer)) {
                std::cout << "Player " << current->playerId << " disconnected during game\n";
                current->connected = false;
                game->endGame("Player disconnected");
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

                std::string result = game->processShot(x, y);

                std::string resultMsg = current->name + " shot at (" + std::to_string(x) + "," + std::to_string(y) + ") - " + result;

                if (!safeSend(current->socket, resultMsg) || !safeSend(opponent->socket, resultMsg)) {
                    game->endGame("Failed to send result message");
                    break;
                }

                if (!game->gameOver && result.find("HIT") == std::string::npos && result.find("Already attacked") == std::string::npos) {
                    game->switchTurn();
                }
            }
            else {
                const std::string errorMsg = "Invalid input format. Use: x y (numbers 0-9)\n";
                safeSend(current->socket, errorMsg);
            }
        }

        if (game->gameOver && game->active) {
            if (game->bothReady()) {
                std::string winMsg = "Congratulations! You won the game!\n";
                std::string loseMsg = "Game over! You lost.\n";

                safeSend(game->currentPlayer->socket, winMsg);
                safeSend(game->getOpponent()->socket, loseMsg);

                std::cout << "Game finished. Winner: Player " << game->currentPlayer->playerId << std::endl;
            }
            else {
                std::string disconnectMsg = "Game ended due to player disconnect.\n";
                if (game->player1->connected) safeSend(game->player1->socket, disconnectMsg);
                if (game->player2->connected) safeSend(game->player2->socket, disconnectMsg);

                std::cout << "Game terminated due to player disconnect" << std::endl;
            }
        }

        // Удаляем игроков
        delete game->player1;
        delete game->player2;

        // Помечаем игру для удаления
        game->active = false;
    }

    void cleanupLoop() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));

            std::lock_guard<std::mutex> lock(gamesMutex);
            auto it = activeGames.begin();
            while (it != activeGames.end()) {
                if (!(*it)->active) {
                    delete* it;
                    it = activeGames.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
    }

    void serverManagementLoop() {
        std::cout << "\nServer commands:\n";
        std::cout << "  /stats - Show server statistics\n";
        std::cout << "  /stop - Stop the server\n";
        std::cout << "  /help - Show this help\n\n";

        while (running) {
            std::string command = InputUtils::getTrimmedInput("> ");

            if (command == "/stats") {
                showStats();
            }
            else if (command == "/stop") {
                std::cout << "Stopping server...\n";
                running = false;
            }
            else if (command == "/help") {
                std::cout << "Available commands:\n";
                std::cout << "  /stats - Show server statistics\n";
                std::cout << "  /stop - Stop the server\n";
                std::cout << "  /help - Show this help\n";
            }
            else if (!command.empty()) {
                std::cout << "Unknown command. Type /help for available commands.\n";
            }
        }
    }

    void showStats() {
        std::lock_guard<std::mutex> lock1(queueMutex);
        std::lock_guard<std::mutex> lock2(gamesMutex);

        std::cout << "\n=== Server Statistics ===\n";
        std::cout << "Waiting players: " << waitingPlayers.size() << "\n";
        std::cout << "Active games: " << activeGames.size() << "\n";
        std::cout << "Total players served: " << (nextPlayerId - 1) << "\n";
        std::cout << "=========================\n\n";
    }
};

int main() {
    std::cout << "=== Sea Battle Server ===\n\n";

    // Получаем порт от пользователя
    int port = InputUtils::getServerPort();

    std::cout << "\nInitializing server on port " << port << "...\n";

    GameServer server(port);

    if (!server.initialize()) {
        std::cerr << "Failed to initialize server\n";
        std::cout << "Press Enter to exit...";
        std::cin.ignore();
        return 1;
    }

    server.start();

    std::cout << "Server shutdown complete\n";
    std::cout << "Press Enter to exit...";
    std::cin.ignore();

    return 0;
}