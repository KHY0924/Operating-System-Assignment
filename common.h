#ifndef COMMON_H
#define  COMMON_H

#include <stdio.h>
#include  <stdlib.h>
#include <string.h>
#include <unistd.h>
#include  <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include   <netinet/in.h>
#include <arpa/inet.h>
#include  <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include   <sys/stat.h>
#include <errno.h>
#include  <semaphore.h>
#include <time.h>

#define PORT  8888
#define MAX_PLAYERS  5
#define  MIN_PLAYERS  3
#define BOARD_SIZE  6
#define WIN_LEN  4
#define  SHM_NAME "/game_shm_v3"
#define LOG_QUEUE_SIZE   100
#define  LOG_MSG_LEN 256
#define BUFFER_SIZE  1024

#define MSG_WELCOME  "WELCOME"
#define  MSG_WAIT "WAIT"
#define MSG_YOUR_TURN   "YOUR_TURN"
#define MSG_VALID_MOVE  "VALID"
#define  MSG_INVALID_MOVE "INVALID"
#define MSG_UPDATE  "UPDATE"
#define MSG_WIN  "WIN"
#define  MSG_LOSE   "LOSE"
#define MSG_DRAW  "DRAW"
#define MSG_GAME_OVER  "GAME_OVER"

typedef  struct {
    int  id;
    int   pid;
    int  active;
    char  name[32];
    char symbol;
    int  score;
}   Player;

typedef struct  {
    char  messages[LOG_QUEUE_SIZE][LOG_MSG_LEN];
    int  head;
    int   tail;
    int count;
}  LogQueue;

typedef  struct   {
    char  name[32];
    int  wins;
}  ScoreRecord;

typedef  struct {
    char  board[BOARD_SIZE][BOARD_SIZE];
    Player   players[MAX_PLAYERS];
    int  playercount;
    int  connected;
    int   started;
    int  gameover;
    int  currentturn;
    int   winner;

    pthread_mutex_t   gamemutex;
    pthread_mutex_t  logmutex;
    sem_t  turnsem[MAX_PLAYERS];
    sem_t  schedsem;
    
    LogQueue  logqueue;
    int   stopflag;

    ScoreRecord  scores[100];
    int  scorecount;

}   GameData;

#endif
