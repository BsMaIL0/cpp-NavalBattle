@echo off
chcp 1251 > nul
setlocal enabledelayedexpansion
g++ -o server.exe NavalBattle_server.cpp -lws2_32 -finput-charset=UTF-8 -fexec-charset=CP1251
g++ -o client.exe NavalBattle_client.cpp -lws2_32 -finput-charset=UTF-8 -fexec-charset=CP1251
start "Naval Battle" server.exe
timeout /t 3 > nul
start "Player 1" client.exe
timeout /t 2 > nul
start "Player 2" client.exe
pause