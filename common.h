#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <semaphore.h>
#include <time.h>

// --- Constants ---
#define PORT 8888
#define MAX_PLAYERS 5
#define MIN_PLAYERS 3
#define BOARD_SIZE 6
#define WIN_LEN 4
#define SHM_NAME "/game_shm_v3"
#define LOG_QUEUE_SIZE 100
#define LOG_MSG_LEN 256
#define BUFFER_SIZE 1024

// --- Protocol Messages ---
#define MSG_WELCOME "WELCOME"
#define MSG_WAIT "WAIT"
#define MSG_YOUR_TURN "YOUR_TURN"
#define MSG_VALID_MOVE "VALID"
#define MSG_INVALID_MOVE "INVALID"
#define MSG_UPDATE "UPDATE"
#define MSG_WIN "WIN"
#define MSG_LOSE "LOSE"
#define MSG_DRAW "DRAW"
#define MSG_GAME_OVER "GAME_OVER"

// --- Structures ---

// Player Info
typedef struct {
    int id;                 // 0 to MAX_PLAYERS-1
    int pid;                // Process ID handling this client
    int active;             // 1 = Connected/Playing, 0 = Inactive
    char name[32];          // Player Name
    char symbol;            // Game Symbol (X, O, #, @, $)
    int score;              // Current session wins (loaded from file)
} Player;

// Logger Queue (Circular Buffer in Shared Memory)
typedef struct {
    char messages[LOG_QUEUE_SIZE][LOG_MSG_LEN];
    int head;
    int tail;
    int count;
} LogQueue;

// Score Database Entry
typedef struct {
    char name[32];
    int wins;
} ScoreEntry;

// Main Shared Memory Structure
typedef struct {
    // Game State
    char board[BOARD_SIZE][BOARD_SIZE];
    Player players[MAX_PLAYERS];
    int num_players;
    int num_connected;
    int game_started;
    int game_over;
    int turn_index;         // Index of player whose turn it is
    int winner_id;          // -1 if none/draw

    // Synchronization
    pthread_mutex_t game_mutex;      // Protects Board & Game State & Scores
    pthread_mutex_t log_mutex;       // Protects Log Queue
    sem_t turn_sem[MAX_PLAYERS];     // Signal specific player to move
    sem_t sched_sem;                 // Signal scheduler that move is done
    
    // Logging
    LogQueue log_queue;
    int stop_threads;                // Flag to stop logger/scheduler on exit

    // Persistence (Member 4)
    ScoreEntry high_scores[100];     // Cache for scores
    int num_scores;

} SharedState;

#endif // COMMON_H
