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

// [START: Shared Memory Data Structures] (共享内存数据结构 - 开始)
typedef  struct {
    int  id;         // [IPC]: 玩家在共享数组中的索引 ID
    int   pid;       // [IPC]: 玩家进程的进程 ID (fork 产生)
    int  active;      // [Shared Data]: 标记玩家是否在线
    char  name[32];   // [Shared Data]: 玩家姓名
    char symbol;      // [Shared Data]: 玩家在棋盘上代表的符号 (X/O)
    int  score;       // [Shared Data]: 玩家本次游戏的得分
}   Player;

typedef struct  {
    char  messages[LOG_QUEUE_SIZE][LOG_MSG_LEN]; // [Shared Data]: 跨进程共享的日志消息队列缓冲区
    int  head;   // [Shared Data]: 循环队列头部索引
    int  tail;   // [Shared Data]: 循环队列尾部索引
    int count;   // [Shared Data]: 队列中当前的日志数量
}  LogQueue;

typedef  struct   {
    char  name[32];  // 评分记录：玩家名
    int  wins;       // 评分记录：胜场数
}  ScoreRecord;

// [IPC & Shared Memory]: 这是映射到所有进程地址空间的核心数据结构
typedef  struct {
    char  board[BOARD_SIZE][BOARD_SIZE]; // [Shared Memory]: 共享棋盘，所有玩家/进程都能看到
    Player   players[MAX_PLAYERS];       // [Shared Memory]: 共享玩家信息数组
    int  playercount;                    // [Shared Memory]: 当前注册的总玩家数
    int  connected;                      // [Shared Memory]: 当前实时连接的玩家数
    int   started;                       // [Shared Memory]: 游戏是否已开始的全局标记
    int  gameover;                       // [Shared Memory]: 游戏是否已结束的全局标记
    int  currentturn;                    // [Shared Memory/Round Robin]: 当前该哪个玩家落子
    int   winner;                        // [Shared Memory]: 获胜者 ID

    pthread_mutex_t   gamemutex;         // [CrossDomain Synch]: 互斥锁，确保对棋盘和状态的修改是原子性的
    pthread_mutex_t  logmutex;           // [CrossDomain Synch]: 互斥锁，防止多个进程同时写入日志队列
    sem_t  turnsem[MAX_PLAYERS];         // [CrossDomain Synch]: 信号量数组，用于调度器通知特定玩家执行动作
    sem_t  schedsem;                     // [CrossDomain Synch]: 信号量，用于玩家通知调度器自己已落子
    
    LogQueue  logqueue;                  // [Shared Memory]: 共享日志队列
    int   stopflag;                      // [Shared Memory]: 通知所有线程/进程停止运行的标记

    ScoreRecord  scores[100];           // [Shared Memory]: 缓存在内存中的评分记录
    int  scorecount;                     // [Shared Memory]: 缓存的分数条目数量

}   GameData;
// [END: Shared Memory Data Structures] (共享内存数据结构 - 结束)

#endif
