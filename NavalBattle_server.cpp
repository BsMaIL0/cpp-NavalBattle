#define _WINSOCK_DEPRECATED_NO_WARNINGS

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

// Константы игры
const int PORT = 12345;
const int BOARD_SIZE = 10;  // Размер игрового поля
const int SHIP_SIZES[] = { 4, 3, 3, 2, 2, 2, 1, 1, 1, 1 };  // Размеры кораблей
const int NUM_SHIPS = 10;  // Количество кораблей
const int BUFFER_SIZE = 256;  // Размер буфера для сетевых операций
const int MAX_PLAYER_NAME = 32;  // Максимальная длина имени игрока

// Состояния клетки на игровом поле
enum CellState {
    EMPTY = 0,    // Пустая клетка
    SHIP = 1,     // Корабль
    HIT = 2,      // Попадание
    MISS = 3,     // Промах
    SUNK = 4      // Потопленный корабль
};

// Структура корабля
struct Ship {
    int size;        // Размер корабля
    int hits;        // Количество попаданий
    bool horizontal; // Ориентация (true - горизонтальная, false - вертикальная)
    int x, y;        // Координаты начала корабля

    // Проверка, потоплен ли корабль
    bool isSunk() const { return hits >= size; }
};

// Класс игрока
class Player {
public:
    SOCKET socket;  // Сокет для связи с клиентом
    std::vector<std::vector<CellState>> board;  // Игровая доска игрока
    std::vector<std::vector<CellState>> enemyView;  // Вид доски противника
    std::vector<Ship> ships;  // Список кораблей игрока
    bool ready;     // Готов ли игрок к игре
    std::string name;  // Имя игрока
    bool connected; // Подключен ли игрок

    // Конструктор
    Player(SOCKET sock) : socket(sock), ready(false), connected(true) {
        // Инициализация пустых досок
        board.resize(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
        enemyView.resize(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
    }

    // Размещение корабля на доске
    bool placeShip(int size, int x, int y, bool horizontal) {
        // Проверка возможности размещения
        if (horizontal) {
            if (x + size > BOARD_SIZE) return false;  // Выход за границы
            for (int i = 0; i < size; i++) {
                if (board[y][x + i] != EMPTY) return false;  // Клетка занята
                // Проверка соседних клеток (правило непрямого касания)
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + i + dx;
                        int ny = y + dy;
                        if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE &&
                            board[ny][nx] == SHIP) return false;  // Рядом уже есть корабль
                    }
                }
            }
        }
        else {
            // Аналогичная проверка для вертикального размещения
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

        // Размещение корабля
        Ship ship{ size, 0, horizontal, x, y };
        ships.push_back(ship);

        // Заполнение клеток корабля на доске
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

    // Функция автоматической расстановки кораблей
    void autoPlaceShips() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, BOARD_SIZE - 1);
        std::uniform_int_distribution<> dis_bool(0, 1);

        // Пытаемся разместить каждый корабль
        for (int i = 0; i < NUM_SHIPS; i++) {
            int size = SHIP_SIZES[i];
            bool placed = false;
            int attempts = 0;
            const int MAX_ATTEMPTS = 100;

            // Попытки разместить корабль в случайной позиции
            while (!placed && attempts < MAX_ATTEMPTS) {
                int x = dis(gen);
                int y = dis(gen);
                bool horizontal = dis_bool(gen) == 1;

                placed = placeShip(size, x, y, horizontal);
                attempts++;
            }

            // Если не удалось разместить - начинаем заново
            if (!placed) {
                board = std::vector<std::vector<CellState>>(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
                ships.clear();
                i = -1; // Начинаем заново с первого корабля
            }
        }
    }

    // Проверка, все ли корабли потоплены
    bool allShipsSunk() {
        for (const auto& ship : ships) {
            if (!ship.isSunk()) return false;
        }
        return true;
    }

    // Отмечает промахи вокруг потопленного корабля на обеих досках
    void markMissesAroundSunkShip(const Ship& ship, Player* opponent) {
        if (ship.horizontal) {
            for (int i = -1; i <= ship.size; i++) {
                for (int dy = -1; dy <= 1; dy++) {
                    int nx = ship.x + i;
                    int ny = ship.y + dy;

                    // Пропускаем клетки самого корабля
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
            // Аналогично для вертикального корабля
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

    // Получение строкового представления доски
    std::string getBoardString(bool showShips = true) {
        std::string result = "  0 1 2 3 4 5 6 7 8 9\n";  // Заголовок с координатами
        for (int y = 0; y < BOARD_SIZE; y++) {
            result += std::to_string(y) + ' ';
            for (int x = 0; x < BOARD_SIZE; x++) {
                char symbol = '.';  // Пустая клетка по умолчанию
                if (board[y][x] == HIT) symbol = 'X';     // Попадание
                else if (board[y][x] == MISS) symbol = 'O'; // Промах
                else if (board[y][x] == SUNK) symbol = '#'; // Потопленный
                else if (showShips && board[y][x] == SHIP) symbol = 'S'; // Корабль
                result += symbol;
                result += ' ';
            }
            result += '\n';
        }
        return result;
    }

    // Получение строкового представления вида противника
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

// Безопасная отправка данных
bool safeSend(SOCKET socket, const std::string& data) {
    if (socket == INVALID_SOCKET) return false;

    const char* buffer = data.c_str();
    int totalSent = 0;
    int length = (int)data.length();

    // Отправка данных частями
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

// Безопасное получение данных
bool safeRecv(SOCKET socket, std::string& data, int maxSize = BUFFER_SIZE) {
    if (socket == INVALID_SOCKET) return false;

    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(socket, buffer, maxSize - 1, 0);

    if (bytesReceived == SOCKET_ERROR) {
        return false;
    }
    else if (bytesReceived == 0) {
        return false; // Соединение закрыто
    }

    buffer[bytesReceived] = '\0';
    data = std::string(buffer, bytesReceived);

    // Очистка от символов форматирования
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

    // Проверка готовности обоих игроков
    bool bothReady() {
        return player1->ready && player2->ready && player1->connected && player2->connected;
    }

    // Смена хода
    void switchTurn() {
        currentPlayer = (currentPlayer == player1) ? player2 : player1;
    }

    // Получение противника текущего игрока
    Player* getOpponent() {
        return (currentPlayer == player1) ? player2 : player1;
    }

    // Обработка выстрела
    std::string processShot(int x, int y) {
        Player* opponent = getOpponent();

        // Проверка валидности координат
        if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
            return "INVALID: Coordinates out of bounds\n";
        }

        std::string result;
        if (opponent->board[y][x] == SHIP) {
            // Попадание в корабль
            opponent->board[y][x] = HIT;
            currentPlayer->enemyView[y][x] = HIT;

            // Поиск корабля, в который попали
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

                // Если корабль потоплен
                if (shipHit && ship.isSunk()) {
                    // Помечаем весь корабль как потопленный
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

                    // Отмечаем промахи вокруг потопленного корабля
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
            // Промах или повторная атака
            if (opponent->board[y][x] == EMPTY) {
                opponent->board[y][x] = MISS;
                currentPlayer->enemyView[y][x] = MISS;
                result = "MISS\n";
            }
            else {
                result = "MISS: Already attacked this position\n";
            }
        }

        // Проверка конца игры
        if (opponent->allShipsSunk()) {
            gameOver = true;
        }

        return result;
    }

    // Проверка подключения игроков
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

// Главная функция сервера
int main() {
    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    // Создание серверного сокета
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Error creating socket: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 2;
    }

    // Настройка опций сокета
    int yes = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR) {
        std::cerr << "Setsockopt failed: " << WSAGetLastError() << "\n";
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 3;
    }

    // Настройка адреса сервера
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    // Привязка сокета
    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << "\n";
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 4;
    }

    // Начало прослушивания
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << "\n";
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 5;
    }

    std::cout << "Server is listening on port " << PORT << "\n";

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
    std::cout << "Player 1 connected!\n";
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
    std::cout << "Player 2 connected!\n";
    Player player2(clientSocket2);
    player2.name = "Player 2";

    // Создание игры
    Game game(&player1, &player2);

    // Лямбда-функция для фазы расстановки кораблей
    auto setupPhase = [](Player& player) {
        const std::string welcome = "Welcome to Sea Battle! Placing ships automatically...\n";
        if (!safeSend(player.socket, welcome)) {
            player.connected = false;
            return false;
        }

        // Автоматическая расстановка кораблей
        player.autoPlaceShips();

        // Отправка информации о расстановке игроку
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

    // Запуск фазы расстановки в отдельных потоках
    std::thread setupThread1(setupPhase, std::ref(player1));
    std::thread setupThread2(setupPhase, std::ref(player2));

    setupThread1.join();
    setupThread2.join();

    // Проверка подключения после расстановки
    if (!player1.connected || !player2.connected) {
        std::cerr << "Player disconnected during setup phase\n";
        safeCloseSocket(clientSocket1);
        safeCloseSocket(clientSocket2);
        safeCloseSocket(serverSocket);
        WSACleanup();
        return 8;
    }

    // Ожидание готовности обоих игроков
    while (!game.bothReady()) {
        Sleep(100);
        if (!game.checkConnections()) {
            std::cerr << "Player disconnected while waiting for readiness\n";
            break;
        }
    }

    // Начало игры
    if (game.bothReady()) {
        const std::string startMsg = "Game started! Player 1 goes first.\n";
        if (!safeSend(player1.socket, startMsg) || !safeSend(player2.socket, startMsg)) {
            game.gameOver = true;
        }
    }

    std::string inputBuffer;

    // Главный игровой цикл
    while (!game.gameOver && game.checkConnections()) {
        Player* current = game.currentPlayer;
        Player* opponent = game.getOpponent();

        // Подготовка сообщений для игроков
        std::string currentTurnMsg = "YOUR_TURN\n";
        currentTurnMsg += "Your board:\n" + current->getBoardString() + "\n";
        currentTurnMsg += "Enemy view:\n" + current->getEnemyViewString() + "\n";
        currentTurnMsg += "Enter coordinates to shoot (x y): ";

        std::string otherTurnMsg = "OPPONENT_TURN\n";
        otherTurnMsg += "Your board:\n" + opponent->getBoardString() + "\n";
        otherTurnMsg += "Enemy view:\n" + opponent->getEnemyViewString() + "\n";
        otherTurnMsg += "Waiting for opponent's move...\n";

        // Отправка сообщений
        if (!safeSend(current->socket, currentTurnMsg) || !safeSend(opponent->socket, otherTurnMsg)) {
            game.gameOver = true;
            break;
        }

        // Получение хода от текущего игрока
        if (!safeRecv(current->socket, inputBuffer)) {
            std::cout << "Player disconnected\n";
            current->connected = false;
            game.gameOver = true;
            break;
        }

        // Парсинг координат
        int x, y;
        char extra;
        if (sscanf_s(inputBuffer.c_str(), "%d %d %c", &x, &y, &extra, 1) == 2) {
            // Дополнительная проверка диапазона
            if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
                const std::string errorMsg = "Invalid coordinates. Use values between 0 and 9.\n";
                safeSend(current->socket, errorMsg);
                continue;
            }

            // Обработка выстрела
            std::string result = game.processShot(x, y);

            // Отправка результата обоим игрокам
            std::string resultMsg = current->name + " shot at (" + std::to_string(x) + "," + std::to_string(y) + ") - " + result;

            if (!safeSend(current->socket, resultMsg) || !safeSend(opponent->socket, resultMsg)) {
                game.gameOver = true;
                break;
            }

            // Смена хода (только при промахе в новую клетку)
            if (!game.gameOver && result.find("HIT") == std::string::npos && result.find("Already attacked") == std::string::npos) {
                game.switchTurn();
            }
        }
        else {
            const std::string errorMsg = "Invalid input format. Use: x y (numbers 0-9)\n";
            safeSend(current->socket, errorMsg);
        }
    }

    // Завершение игры
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

    // Завершение работы сервера
    safeCloseSocket(clientSocket1);
    safeCloseSocket(clientSocket2);
    safeCloseSocket(serverSocket);
    WSACleanup();

    std::cout << "Server shutdown complete\n";
    return 0;
}