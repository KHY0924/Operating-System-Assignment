# Mega Tic-Tac-Toe (Networked) - OS Assignment

## Overview
This project implements a multiplayer (3-5 players), text-based "Mega Tic-Tac-Toe" game using a **Hybrid Concurrency Model**. It combines **Multiprocessing (fork)** for client connection handling and **Multithreading (pthreads)** for internal server tasks (Scheduler and Logger), synchronized via **POSIX Shared Memory** and **Semaphores**.

## System Requirements
- Linux Environment (e.g., Ubuntu, WSL)
- GCC Compiler with pthread support

## Compilation
To compile both the server and client, run:

```bash
make
```

This will generate two executables: `server` and `client`.

To clean up build files:
```bash
make clean
```

## Running the Game

### 1. Start the Server
Run the server on a machine. You can optionally specify a port (default 8888).
```bash
./server [PORT]
# Example:
./server
```
The server will initialize shared memory (`/game_shm_v3`), load scores from `scores.txt`, and start waiting for connections.

### 2. Start Clients
Run the client. If the server is on the same machine, use `127.0.0.1`. If on a different machine, use the server's IP address.
```bash
./client [SERVER_IP]
# Example (Local):
./client
# Example (Remote):
./client 192.168.1.50
```

### 3. Gameplay
1.  **Connect**: Requires exactly **3 to 5 players** to start.
2.  **Wait**: The game will automatically start once the minimum number of players (3) have joined.
3.  **Turns**: The server manages turns in a Round-Robin fashion.
    - When it is your turn, you will see the board.
    - Enter your move as `ROW COL` (e.g., `2 3`).
    - The goal is to get **4 symbols in a row** (Horizontal, Vertical, or Diagonal).
4.  **End**: The game ends when a player wins or the board is full (Draw). Scores are saved automatically.

## Game Rules
- **Board Size**: 6x6
- **Win Condition**: 4 consecutive symbols.
- **Players**: 3 to 5.
- **Symbols**: Player 1 (X), Player 2 (O), Player 3 (#), Player 4 (@), Player 5 ($).

## Architecture Features
- **Hybrid Concurrency**: 
    - `fork()`: Used for each client connection (child process).
    - `pthread`: Used for `Scheduler` (turn management) and `Logger` (file I/O) threads.
- **IPC**: Uses `shm_open` and `mmap` for shared state.
- **Synchronization**: Process-shared mutexes (`pthread_mutex_t`) and semaphores (`sem_t`) protect the game board, log queue, and turn signalling.
- **Persistence**: Player win counts are stored in `scores.txt` and loaded/saved atomically.
- **Logging**: All events are logged to `game.log` by a dedicated logger thread.
