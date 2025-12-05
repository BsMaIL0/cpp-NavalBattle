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
#include <map>
#include <sstream>
#include <cctype>

#pragma comment(lib, "ws2_32.lib")

const int PORT = 12345;
const int BOARD_SIZE = 10;
const int SHIP_SIZES[] = { 4, 3, 3, 2, 2, 2, 1, 1, 1, 1 };
const int NUM_SHIPS = 10;
const int BUFFER_SIZE = 4096;

enum CellState {
    EMPTY = 0, SHIP = 1, HIT = 2, MISS = 3, SUNK = 4
};

struct Ship {
    int size;
    int hits;
    bool horizontal;
    int x, y;

    bool isSunk() const {
        return hits >= size;
    }
};

class Player {
public:
    SOCKET socket;
    std::vector<std::vector<CellState>> board;
    std::vector<std::vector<CellState>> enemyView;
    std::vector<Ship> ships;
    bool ready;
    std::string name;
    bool connected;
    std::map<int, int> placedShips;

    Player(SOCKET sock) : socket(sock), ready(false), connected(true) {
        board.resize(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
        enemyView.resize(BOARD_SIZE, std::vector<CellState>(BOARD_SIZE, EMPTY));
        placedShips = { {1, 0}, {2, 0}, {3, 0}, {4, 0} };
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

    bool removeShipAt(int x, int y) {
        for (auto it = ships.begin(); it != ships.end(); ++it) {
            if (it->horizontal) {
                for (int i = 0; i < it->size; i++) {
                    if (it->x + i == x && it->y == y) {
                        for (int j = 0; j < it->size; j++) {
                            int shipX = it->x + j;
                            int shipY = it->y;
                            board[shipY][shipX] = EMPTY;
                        }
                        placedShips[it->size]--;
                        ships.erase(it);
                        return true;
                    }
                }
            }
            else {
                for (int i = 0; i < it->size; i++) {
                    if (it->x == x && it->y + i == y) {
                        for (int j = 0; j < it->size; j++) {
                            int shipX = it->x;
                            int shipY = it->y + j;
                            board[shipY][shipX] = EMPTY;
                        }
                        placedShips[it->size]--;
                        ships.erase(it);
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool allShipsPlaced() const {
        return placedShips.at(1) == 4 &&
            placedShips.at(2) == 3 &&
            placedShips.at(3) == 2 &&
            placedShips.at(4) == 1;
    }

    bool allShipsSunk() const {
        if (ships.empty()) return false;

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

    // КРАСИВАЯ ДОСКА С БУКВАМИ A-J И ПРОБЕЛОМ ПЕРЕД БУКВАМИ
    std::string getBoardString(bool showShips = true) {
        std::string result;

        // Заголовок с буквами (добавляем пробел для выравнивания)
        result += "   ";
        for (char c = 'A'; c < 'A' + BOARD_SIZE; c++) {
            result += c;
            result += ' ';
        }
        result += "\n";

        // Игровое поле
        for (int y = 0; y < BOARD_SIZE; y++) {
            // Номер строки (1-10) с выравниванием
            if (y + 1 < 10) {
                result += " " + std::to_string(y + 1) + " ";
            }
            else {
                result += std::to_string(y + 1) + " ";
            }

            for (int x = 0; x < BOARD_SIZE; x++) {
                char symbol = '.';
                if (board[y][x] == HIT) symbol = 'X';
                else if (board[y][x] == MISS) symbol = 'O';
                else if (board[y][x] == SUNK) symbol = '#';
                else if (showShips && board[y][x] == SHIP) symbol = 'S';
                result += symbol;
                result += ' ';
            }
            result += "\n";
        }
        return result;
    }

    std::string getEnemyViewString() {
        std::string result;

        result += "   ";
        for (char c = 'A'; c < 'A' + BOARD_SIZE; c++) {
            result += c;
            result += ' ';
        }
        result += "\n";

        for (int y = 0; y < BOARD_SIZE; y++) {
            if (y + 1 < 10) {
                result += " " + std::to_string(y + 1) + " ";
            }
            else {
                result += std::to_string(y + 1) + " ";
            }

            for (int x = 0; x < BOARD_SIZE; x++) {
                char symbol = '.';
                if (enemyView[y][x] == HIT) symbol = 'X';
                else if (enemyView[y][x] == MISS) symbol = 'O';
                else if (enemyView[y][x] == SUNK) symbol = '#';
                result += symbol;
                result += ' ';
            }
            result += "\n";
        }
        return result;
    }
};

bool safeSend(SOCKET socket, const std::string& data) {
    if (socket == INVALID_SOCKET) return false;

    const char* buffer = data.c_str();
    int totalSent = 0;
    int length = (int)data.length();

    while (totalSent < length) {
        int sent = send(socket, buffer + totalSent, length - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            return false;
        }
        totalSent += sent;
    }
    return true;
}

bool safeRecv(SOCKET socket, std::string& data) {
    if (socket == INVALID_SOCKET) return false;

    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(socket, buffer, BUFFER_SIZE - 1, 0);

    if (bytesReceived <= 0) {
        return false;
    }

    buffer[bytesReceived] = '\0';
    data = std::string(buffer, bytesReceived);

    data.erase(std::remove(data.begin(), data.end(), '\r'), data.end());
    data.erase(std::remove(data.begin(), data.end(), '\n'), data.end());

    return true;
}

// Функция для парсинга координат "A1", "B5" и т.д.
bool parseCell(const std::string& cell, int& x, int& y) {
    if (cell.size() < 2) return false;

    char colChar = std::toupper(cell[0]);
    if (colChar < 'A' || colChar > 'J') return false;
    x = colChar - 'A';

    std::string numStr = cell.substr(1);
    for (char ch : numStr) {
        if (!std::isdigit(ch)) return false;
    }

    int row = std::stoi(numStr);
    if (row < 1 || row > 10) return false;
    y = row - 1;

    return true;
}

// Функция для парсинга координат выстрела (поддержка "A1" и "x y")
bool parseCoordinates(const std::string& input, int& x, int& y) {
    // Пробуем формат "A1", "B5" и т.д.
    if (parseCell(input, x, y)) {
        return true;
    }

    // Пробуем старый формат "x y"
    std::istringstream iss(input);
    if (iss >> x >> y) {
        // Проверяем что нет лишних символов
        std::string extra;
        if (!(iss >> extra)) {
            return (x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE);
        }
    }

    return false;
}

class Game {
public:
    Player* player1;
    Player* player2;
    bool gameStarted;
    bool gameOver;
    Player* currentPlayer;

    Game(Player* p1, Player* p2) : player1(p1), player2(p2),
        gameStarted(false), gameOver(false),
        currentPlayer(p1) {}

    bool bothReady() {
        return player1->ready && player2->ready;
    }

    void switchTurn() {
        currentPlayer = (currentPlayer == player1) ? player2 : player1;
        std::cout << "[ОТЛАДКА] Ход переключен на: " << currentPlayer->name << std::endl;
    }

    Player* getOpponent() {
        return (currentPlayer == player1) ? player2 : player1;
    }

    bool manualPlacementPhase(Player& player) {
        std::string welcome = "=== РУЧНАЯ РАССТАНОВКА КОРАБЛЕЙ ===\n";
        welcome += "Игрок: " + player.name + "\n\n";
        welcome += "Вам нужно расставить:\n";
        welcome += "- 1 корабль на 4 клетки\n";
        welcome += "- 2 корабля на 3 клетки\n";
        welcome += "- 3 корабля на 2 клетки\n";
        welcome += "- 4 корабля на 1 клетку\n\n";
        welcome += "Команды:\n";
        welcome += "ADD <размер> <начало> <конец> - добавить корабль (пример: ADD 3 A1 A3)\n";
        welcome += "DEL <клетка> - удалить корабль (пример: DEL A1)\n";
        welcome += "AUTO - автоматическая расстановка\n";
        welcome += "DONE - завершить расстановку\n";
        welcome += "QUIT - выйти из игры\n\n";

        if (!safeSend(player.socket, welcome)) {
            player.connected = false;
            return false;
        }

        std::string input;
        while (true) {
            std::string status = "Текущая расстановка:\n";
            status += player.getBoardString(true) + "\n";
            status += "Статистика:\n";
            status += "- 4-палубные: " + std::to_string(player.placedShips[4]) + "/1\n";
            status += "- 3-палубные: " + std::to_string(player.placedShips[3]) + "/2\n";
            status += "- 2-палубные: " + std::to_string(player.placedShips[2]) + "/3\n";
            status += "- 1-палубные: " + std::to_string(player.placedShips[1]) + "/4\n\n";
            status += "Введите команду: ";

            if (!safeSend(player.socket, status)) {
                player.connected = false;
                return false;
            }

            if (!safeRecv(player.socket, input)) {
                std::cout << "[ОТЛАДКА] Игрок " << player.name << " отключился\n";
                player.connected = false;
                return false;
            }

            if (input.empty()) continue;

            std::istringstream iss(input);
            std::string command;
            iss >> command;

            if (command == "DONE" || command == "done") {
                if (player.allShipsPlaced()) {
                    player.ready = true;
                    std::cout << "[ОТЛАДКА] Игрок " << player.name << " готов к игре\n";
                    safeSend(player.socket, "Все корабли расставлены! Ожидаем второго игрока...\n");
                    return true;
                }
                else {
                    safeSend(player.socket, "ОШИБКА: Не все корабли расставлены!\n");
                }
            }
            else if (command == "QUIT" || command == "quit") {
                player.connected = false;
                return false;
            }
            else if (command == "AUTO" || command == "auto") {
                player.board = std::vector<std::vector<CellState>>(BOARD_SIZE,
                    std::vector<CellState>(BOARD_SIZE, EMPTY));
                player.ships.clear();
                player.placedShips = { {1, 0}, {2, 0}, {3, 0}, {4, 0} };

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

                        placed = player.placeShip(size, x, y, horizontal);
                        if (placed) {
                            player.placedShips[size]++;
                        }
                        attempts++;
                    }

                    if (!placed) {
                        player.board = std::vector<std::vector<CellState>>(BOARD_SIZE,
                            std::vector<CellState>(BOARD_SIZE, EMPTY));
                        player.ships.clear();
                        player.placedShips = { {1, 0}, {2, 0}, {3, 0}, {4, 0} };
                        i = -1;
                    }
                }

                safeSend(player.socket, "Корабли расставлены автоматически!\n");
            }
            else if (command == "ADD" || command == "add") {
                int size;
                std::string startStr, endStr;

                if (!(iss >> size >> startStr >> endStr)) {
                    safeSend(player.socket, "ОШИБКА: Неверный формат\n");
                    continue;
                }

                if (size < 1 || size > 4) {
                    safeSend(player.socket, "ОШИБКА: Размер от 1 до 4\n");
                    continue;
                }

                int maxShips = 5 - size;
                if (player.placedShips[size] >= maxShips) {
                    safeSend(player.socket, "ОШИБКА: Максимум " + std::to_string(maxShips) +
                        " кораблей размером " + std::to_string(size) + "\n");
                    continue;
                }

                int x1, y1, x2, y2;
                if (!parseCell(startStr, x1, y1) || !parseCell(endStr, x2, y2)) {
                    safeSend(player.socket, "ОШИБКА: Неверные координаты\n");
                    continue;
                }

                if (x1 != x2 && y1 != y2) {
                    safeSend(player.socket, "ОШИБКА: Корабль должен быть прямым\n");
                    continue;
                }

                bool horizontal = (y1 == y2);
                int actualSize;

                if (horizontal) {
                    actualSize = std::abs(x2 - x1) + 1;
                    if (actualSize != size) {
                        safeSend(player.socket, "ОШИБКА: Размер не совпадает\n");
                        continue;
                    }
                    int startX = (x1 < x2) ? x1 : x2;

                    if (player.placeShip(size, startX, y1, true)) {
                        player.placedShips[size]++;
                        safeSend(player.socket, "Корабль добавлен успешно!\n");
                    }
                    else {
                        safeSend(player.socket, "ОШИБКА: Нельзя разместить здесь\n");
                    }
                }
                else {
                    actualSize = std::abs(y2 - y1) + 1;
                    if (actualSize != size) {
                        safeSend(player.socket, "ОШИБКА: Размер не совпадает\n");
                        continue;
                    }
                    int startY = (y1 < y2) ? y1 : y2;

                    if (player.placeShip(size, x1, startY, false)) {
                        player.placedShips[size]++;
                        safeSend(player.socket, "Корабль добавлен успешно!\n");
                    }
                    else {
                        safeSend(player.socket, "ОШИБКА: Нельзя разместить здесь\n");
                    }
                }
            }
            else if (command == "DEL" || command == "del") {
                std::string cellStr;
                iss >> cellStr;

                int x, y;
                if (!parseCell(cellStr, x, y)) {
                    safeSend(player.socket, "ОШИБКА: Неверные координаты\n");
                    continue;
                }

                if (player.removeShipAt(x, y)) {
                    safeSend(player.socket, "Корабль удален успешно!\n");
                }
                else {
                    safeSend(player.socket, "ОШИБКА: Здесь нет корабля\n");
                }
            }
            else {
                safeSend(player.socket, "ОШИБКА: Неизвестная команда\n");
            }
        }
    }

    std::string processShot(int x, int y) {
        Player* opponent = getOpponent();

        std::cout << "[ОТЛАДКА] " << currentPlayer->name << " стреляет в ("
            << char('A' + x) << y + 1 << ")\n";

        if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
            return "НЕВЕРНО: Координаты вне поля\n";
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
                    result = "ПОПАДАНИЕ! Корабль потоплен!\n";
                    break;
                }
                else if (shipHit) {
                    result = "ПОПАДАНИЕ!\n";
                    break;
                }
            }
        }
        else {
            if (opponent->board[y][x] == EMPTY) {
                opponent->board[y][x] = MISS;
                currentPlayer->enemyView[y][x] = MISS;
                result = "ПРОМАХ!\n";
            }
            else {
                result = "ПРОМАХ: Уже стреляли в эту клетку\n";
            }
        }

        if (opponent->allShipsSunk()) {
            std::cout << "[ОТЛАДКА] Игра окончена! " << currentPlayer->name << " победил!\n";
            gameOver = true;
        }

        return result;
    }
};

int main() {
    setlocale(LC_ALL, "Russian");

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Ошибка инициализации Winsock\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Ошибка создания сокета: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 2;
    }

    int yes = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR) {
        std::cerr << "Ошибка setsockopt: " << WSAGetLastError() << "\n";
        closesocket(serverSocket);
        WSACleanup();
        return 3;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Ошибка привязки: " << WSAGetLastError() << "\n";
        closesocket(serverSocket);
        WSACleanup();
        return 4;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Ошибка прослушивания: " << WSAGetLastError() << "\n";
        closesocket(serverSocket);
        WSACleanup();
        return 5;
    }

    std::cout << "Сервер запущен на порту " << PORT << "\n";
    std::cout << "Ожидание игроков...\n";

    SOCKADDR_IN clientAddr1;
    int clientAddrSize = sizeof(clientAddr1);
    SOCKET clientSocket1 = accept(serverSocket, (SOCKADDR*)&clientAddr1, &clientAddrSize);
    if (clientSocket1 == INVALID_SOCKET) {
        std::cerr << "Ошибка подключения игрока 1\n";
        closesocket(serverSocket);
        WSACleanup();
        return 6;
    }
    std::cout << "Игрок 1 подключен!\n";
    Player player1(clientSocket1);
    player1.name = "Игрок 1";

    SOCKADDR_IN clientAddr2;
    SOCKET clientSocket2 = accept(serverSocket, (SOCKADDR*)&clientAddr2, &clientAddrSize);
    if (clientSocket2 == INVALID_SOCKET) {
        std::cerr << "Ошибка подключения игрока 2\n";
        closesocket(clientSocket1);
        closesocket(serverSocket);
        WSACleanup();
        return 7;
    }
    std::cout << "Игрок 2 подключен!\n";
    Player player2(clientSocket2);
    player2.name = "Игрок 2";

    Game game(&player1, &player2);

    std::cout << "Начало расстановки кораблей...\n";

    auto setupPhase = [&game](Player& player) {
        return game.manualPlacementPhase(player);
        };

    std::thread setupThread1(setupPhase, std::ref(player1));
    std::thread setupThread2(setupPhase, std::ref(player2));

    setupThread1.join();
    setupThread2.join();

    std::cout << "\n=== РАССТАНОВКА ЗАВЕРШЕНА ===\n";
    std::cout << "Кораблей у Игрока 1: " << player1.ships.size() << std::endl;
    std::cout << "Кораблей у Игрока 2: " << player2.ships.size() << std::endl;

    if (player1.ships.empty() || player2.ships.empty()) {
        std::cerr << "ОШИБКА: У одного из игроков нет кораблей!\n";
        closesocket(clientSocket1);
        closesocket(clientSocket2);
        closesocket(serverSocket);
        WSACleanup();
        return 8;
    }

    while (!game.bothReady()) {
        Sleep(100);
    }

    if (game.bothReady()) {
        std::string startMsg = "=== ИГРА НАЧИНАЕТСЯ ===\n";
        startMsg += "Все игроки готовы!\n";
        startMsg += "Первым ходит Игрок 1.\n\n";

        safeSend(player1.socket, startMsg);
        safeSend(player2.socket, startMsg);
        game.gameStarted = true;

        std::cout << "Игра началась!\n";
    }

    std::string inputBuffer;

    // ГЛАВНЫЙ ИГРОВОЙ ЦИКЛ
    while (!game.gameOver) {
        Player* current = game.currentPlayer;
        Player* opponent = game.getOpponent();

        std::cout << "\n=== ХОД: " << current->name << " ===" << std::endl;

        // 1. Отправляем ход текущему игроку
        std::string turnMsg = "=== ВАШ ХОД ===\n";
        turnMsg += "Ваше поле:\n" + current->getBoardString() + "\n";
        turnMsg += "Поле противника:\n" + current->getEnemyViewString() + "\n";
        turnMsg += "Введите координаты для выстрела (например: A1, B5): ";

        std::cout << "[ОТЛАДКА] Отправка сообщения о ходе " << current->name << std::endl;
        if (!safeSend(current->socket, turnMsg)) {
            std::cout << "[ОШИБКА] Не удалось отправить " << current->name << std::endl;
            break;
        }

        // 2. Ждем ход от текущего игрока
        std::cout << "[ОТЛАДКА] Ожидание хода от " << current->name << std::endl;
        if (!safeRecv(current->socket, inputBuffer)) {
            std::cout << "[ОШИБКА] Не удалось получить ход от " << current->name << std::endl;
            break;
        }

        // 3. Парсим координаты (поддержка "A1" и "x y")
        int x, y;
        if (parseCoordinates(inputBuffer, x, y)) {
            // 4. Обрабатываем выстрел
            std::string result = game.processShot(x, y);

            // Формируем координаты в формате "A1"
            char column = 'A' + x;
            int row = y + 1;
            std::string coordStr = std::string(1, column) + std::to_string(row);

            std::string resultMsg = "\n" + current->name + " выстрелил в " + coordStr + " - " + result;

            std::cout << "Результат: " << resultMsg;

            // 5. Отправляем результат обоим игрокам
            safeSend(current->socket, resultMsg);
            safeSend(opponent->socket, resultMsg);

            // 6. Смена хода только при промахе
            if (!game.gameOver && result.find("ПОПАДАНИЕ") == std::string::npos) {
                game.switchTurn();
            }

            Sleep(1000);  // Пауза между ходами
        }
        else {
            std::string errorMsg = "Неверные координаты. Используйте формат: A1, B5 и т.д. (A-J, 1-10)\n";
            safeSend(current->socket, errorMsg);
        }
    }

    if (game.gameOver) {
        std::string winMsg = "\n=== ПОБЕДА! ===\nПоздравляем! Вы выиграли!\n";
        std::string loseMsg = "\n=== ПОРАЖЕНИЕ ===\nИгра окончена! Вы проиграли.\n";

        safeSend(game.currentPlayer->socket, winMsg);
        safeSend(game.getOpponent()->socket, loseMsg);

        std::cout << "\n=== ИГРА ОКОНЧЕНА ===\n";
        std::cout << "Победитель: " << game.currentPlayer->name << std::endl;
    }

    closesocket(clientSocket1);
    closesocket(clientSocket2);
    closesocket(serverSocket);
    WSACleanup();

    std::cout << "Сервер завершил работу\n";
    return 0;
}
