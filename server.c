#include "common.h"

GameData  *gamedata;
int  serverfd;
int  port   =  PORT;

void  logerror( const char  *funcname,   const char  *message)  {
    FILE  *file  =  fopen( "error.log",   "a");
    if ( file)  {
        time_t  now  =  time( NULL);
        struct tm  *timeinfo   =  localtime( &now);
        char  timestring[ 64];
        strftime( timestring,   sizeof( timestring),  "%Y-%m-%d %H:%M:%S",  timeinfo);
        fprintf( file,  "[%s] ERROR in %s: %s\n",   timestring,  funcname,  message);
        fflush( file);
        fclose( file);
    }
    fprintf( stderr,  "[ERROR] %s: %s\n",   funcname,  message);
}

void  exitwitherror( const  char   *message)  {
    logerror( "FATAL",  message);
    perror( message);
    exit( EXIT_FAILURE);
}

// [Multithreading/IPC]: 向共享内存日志队列中安全地添加一条消息
void  addtolog( const char  *message)  { // 添加日志条目的函数
    if ( !gamedata)   return; // 如果共享内存未初始化，直接返回

    time_t  now  =   time( NULL); // 获取当前系统时间
    struct tm  *timeinfo  =  localtime( &now); // 转换为本地时间结构
    char  timestring[ 64]; // 时间字符串缓冲区
    strftime( timestring,  sizeof( timestring),   "%Y-%m-%d %H:%M:%S",  timeinfo); // 格式化时间

    char  fullmessage[ LOG_MSG_LEN]; // 日志全消息缓冲区
    snprintf( fullmessage,   LOG_MSG_LEN,  "[%s] %s",  timestring,  message); // 合并时间与内容

    pthread_mutex_lock( &gamedata->logmutex); // [Synchronization]: 加锁，保护队列 head 等变量，防止多线程/多进程同时写入冲突
    
    int  nexthead  =   ( gamedata->logqueue.head  +  1)  %  LOG_QUEUE_SIZE; // 计算下一个头部索引
    if ( nexthead  !=   gamedata->logqueue.tail)  { // 如果队列未满
        strncpy( gamedata->logqueue.messages[ gamedata->logqueue.head],   fullmessage,  LOG_MSG_LEN); // 将消息考入共享内存缓冲区
        gamedata->logqueue.head  =  nexthead; // 更新头部索引
        gamedata->logqueue.count++; // 增加计数
    }

    pthread_mutex_unlock( &gamedata->logmutex); // [Synchronization]: 解锁，完成线程同步
}

// [START: Concurrent Logger] (并发日志记录器 - 开始)
// [Multithreading (pthreads)] - 这是一个独立的线程，负责并发记录日志
void  *loggerthread( void   *arg)  { // 线程主函数：日志线程入口
    printf( "[Logger Thread] Started.\n"); // 打印信息表示日志线程已经成功开启
    FILE  *logfile  =  fopen( "game.log",   "a"); // 以“追加模式” (append) 打开或创建 game.log 文件
    if ( !logfile)  { // 如果文件打开失败
        logerror( "loggerthread",   "Failed to open game.log for writing"); // 记录错误到错误日志
        perror( "Failed to open game.log"); // 输出标准错误信息
        return  NULL; // 退出线程，返回空指针
    }

    setbuf( logfile,   NULL); // 禁用文件的应用级缓冲，确保日志能立即写入磁盘

    while ( !gamedata->stopflag)  { // 当服务器未发出停止信号时，持续循环
        int  didwork   =  0; // 用于判断本轮循环是否处理了日志条目
        
        pthread_mutex_lock( &gamedata->logmutex); // 加锁：保护共享内存中的日志队列，防止竞态条件
        if ( gamedata->logqueue.tail   !=  gamedata->logqueue.head)  { // 检查队列尾部和头部是否相等，如果不相等说明有新日志
            fprintf( logfile,  "%s\n",   gamedata->logqueue.messages[ gamedata->logqueue.tail]); // 将队列尾部的日志消息写入文件
            fflush( logfile); // 强制刷新流，确保数据写入物理文件
            gamedata->logqueue.tail  =   ( gamedata->logqueue.tail  +  1)  %  LOG_QUEUE_SIZE; // 将队列尾部索引后移，实现循环队列
            gamedata->logqueue.count--; // 递减当前队列中的日志计数
            didwork  =  1; // 标记本轮循环执行了写操作
        }
        pthread_mutex_unlock( &gamedata->logmutex); // 解锁：允许其他线程（如 addtolog）访问队列

        if ( !didwork)  { // 如果本轮没工作可做（队列为空）
            usleep( 50000); // 挂起 50 毫秒，避免空循环空耗 CPU 资源
        }
    }

    fclose( logfile); // 循环结束（服务器关闭），关闭日志文件
    printf( "[Logger Thread] Stopped.\n"); // 打印日志线程停止的信息
    return   NULL; // 线程正常结束
}
// [END: Concurrent Logger] (并发日志记录器 - 结束)

// [START: Persistent Scoring] (持久化评分 - 开始)
void  loadscores()  {
    if ( !gamedata)  return;

    pthread_mutex_lock( &gamedata->gamemutex);
    gamedata->scorecount   =  0;
    FILE  *file  =  fopen( "scores.txt",   "r");
    if ( !file)  {
        logerror( "loadscores",  "scores.txt not found, creating new file");
        file  =  fopen( "scores.txt",   "w");
        if( file)   fclose( file);
        else  logerror( "loadscores",   "Failed to create scores.txt");
        pthread_mutex_unlock( &gamedata->gamemutex);
        return;
    }

    char  name[ 32];
    int  wins;
    while( fscanf( file,  "%s %d",  name,   &wins)  ==  2  &&  gamedata->scorecount  <  100)  {
        strncpy( gamedata->scores[ gamedata->scorecount].name,  name,   31);
        gamedata->scores[ gamedata->scorecount].wins   =  wins;
        gamedata->scorecount++;
    }
    fclose( file);
    pthread_mutex_unlock( &gamedata->gamemutex);
    
    char  logmessage[ 100];
    snprintf( logmessage,  100,   "PERSISTENCE: Loaded %d scores.",  gamedata->scorecount);
    addtolog( logmessage);
}

void  savescore( const char  *playername,   int  addwins)  {
    if ( !gamedata  ||   !playername)  return;

    int  found  =  0;
    for ( int i  =  0;   i  <  gamedata->scorecount;  i++)  {
        if ( strcmp( gamedata->scores[i].name,   playername)  ==  0)  {
            gamedata->scores[i].wins  +=   addwins;
            found  =  1;
            break;
        }
    }

    if ( !found  &&  gamedata->scorecount   <  100)  {
        strncpy( gamedata->scores[ gamedata->scorecount].name,   playername,  31);
        gamedata->scores[ gamedata->scorecount].wins  =  addwins;
        gamedata->scorecount++;
    }

    if ( gamedata->scorecount   ==  0)  {
        printf( "[Score Debug] Warning: No scores to save (scorecount=0). Skipping write to prevent data loss.\n");
        return;
    }

    FILE  *file  =  fopen( "scores.txt",   "w");
    if ( file)  {
        for ( int i  =  0;  i  <  gamedata->scorecount;   i++)  {
            fprintf( file,  "%s %d\n",  gamedata->scores[i].name,   gamedata->scores[i].wins);
        }
        fclose( file);
        printf( "[Score Debug] Successfully wrote %d scores to scores.txt\n",   gamedata->scorecount);
    }  else  {
        logerror( "savescore",   "Failed to open scores.txt for writing");
        perror( "[Score Debug] Failed to open scores.txt for writing");
    }
    
    char  logmessage[ 128];
    snprintf( logmessage,  128,  "PERSISTENCE: Saved score for %s. Total scores in memory: %d",   playername,  gamedata->scorecount);
    addtolog( logmessage);
}

void  saveallscores()  {
    if ( !gamedata)   return;
    pthread_mutex_lock( &gamedata->gamemutex);
    FILE  *file  =  fopen( "scores.txt",  "w");
    if   ( file)  {
        for ( int i  =  0;  i  <  gamedata->scorecount;   i++)  {
            fprintf( file,  "%s %d\n",   gamedata->scores[i].name,  gamedata->scores[i].wins);
        }
        fclose( file);
    }  else  {
        logerror( "saveallscores",   "Failed to open scores.txt for writing on shutdown");
    }
    pthread_mutex_unlock( &gamedata->gamemutex);
}
// [END: Persistent Scoring] (持久化评分 - 结束)


// [START: StudentChosen Game] (学生自选游戏 - 开始)
int  checkwin( char  symbol)  {
    for ( int row  =  0;   row  <  BOARD_SIZE;  row++)  {
        for ( int col  =  0;  col  <=   BOARD_SIZE  -  WIN_LEN;  col++)  {
            int  count  =  0;
            for ( int k  =  0;   k  <  WIN_LEN;  k++)  {
                if ( gamedata->board[row][col+k]   ==  symbol)  count++;
            }
            if ( count  ==  WIN_LEN)   return  1;
        }
    }
    for ( int col  =  0;  col  <  BOARD_SIZE;   col++)  {
        for ( int row  =  0;  row  <=  BOARD_SIZE  -  WIN_LEN;   row++)  {
            int  count  =  0;
            for ( int k  =  0;   k  <  WIN_LEN;  k++)  {
                if ( gamedata->board[row+k][col]  ==  symbol)   count++;
            }
            if ( count  ==   WIN_LEN)  return  1;
        }
    }
    for ( int row  =  0;  row  <=  BOARD_SIZE  -  WIN_LEN;   row++)  {
        for ( int col  =  0;   col  <=  BOARD_SIZE  -  WIN_LEN;  col++)  {
            int  count  =  0;
            for ( int k  =  0;  k  <  WIN_LEN;   k++)  {
                if ( gamedata->board[row+k][col+k]   ==  symbol)  count++;
            }
            if ( count  ==  WIN_LEN)  return   1;
        }
    }
    for ( int row  =  0;   row  <=  BOARD_SIZE  -  WIN_LEN;  row++)  {
        for ( int col  =  WIN_LEN  -  1;   col  <  BOARD_SIZE;  col++)  {
            int  count  =  0;
            for ( int k  =  0;  k  <  WIN_LEN;  k++)  {
                if ( gamedata->board[row+k][col-k]   ==  symbol)  count++;
            }
            if ( count  ==  WIN_LEN)   return  1;
        }
    }
    return  0;
}

int  isboardfull()  {
    for( int i=0;  i<BOARD_SIZE;   i++)
        for( int j=0;  j<BOARD_SIZE;  j++)
            if( gamedata->board[i][j]  ==   ' ')  return  0;
    return  1;
}
// [END: StudentChosen Game] (学生自选游戏 - 结束)

void  resetgame()  {
    pthread_mutex_lock( &gamedata->gamemutex);
    memset( gamedata->board,  ' ',   sizeof( gamedata->board));
    gamedata->started  =  0;
    gamedata->gameover   =  0;
    gamedata->winner  =  -1;
    pthread_mutex_unlock( &gamedata->gamemutex);
    addtolog( "GAME: Board reset.");
}

// [START: Round Robin Scheduler] (轮转调度器 - 开始)
// [Multithreading (pthreads) / Round Robin Scheduler] - 这个线程模拟操作系统调度器
void  *schedulerthread( void  *arg)  { // 调度器线程主逻辑
    printf( "[Scheduler Thread] Started.\n"); // 启动提示

    while( !gamedata->stopflag)  { // 主调度循环
        
        pthread_mutex_lock( &gamedata->gamemutex); // 获取游戏数据的全局锁
        int  connectedcount  =   gamedata->connected; // 读取当前已连接的玩家人数
        int  gamestarted  =  gamedata->started; // 读取游戏是否已开始的状态
        pthread_mutex_unlock( &gamedata->gamemutex); // 释放锁

        if ( !gamestarted)  { // 如果游戏尚未开始
            if ( connectedcount  >=   MIN_PLAYERS)  { // 检查是否达到了最小开赛人数
                if ( connectedcount   <  MAX_PLAYERS)  { // 人数达到最小但未满员
                    printf( "[Scheduler] Minimum players met. Waiting 15s for others to join...\n"); // 等待更多人
                    addtolog( "SCHEDULER: Minimum players met. Waiting 15s for others..."); // 记录日志
                    sleep( 15); // 线程休眠 15 秒（模拟 OS 等待资源或就绪队列）
                }  else  { // 人数已满
                    addtolog( "SCHEDULER: Max players reached. Starting immediately!"); // 直接开赛
                } 
                
                pthread_mutex_lock( &gamedata->gamemutex); // 准备修改共享内存中的游戏状态
                gamedata->started   =  1; // 标记游戏正式开始
                gamedata->gameover  =  0; // 重置游戏结束标志
                gamedata->winner  =  -1; // 重置获胜者信息
                gamedata->currentturn   =  0; // 初始化第一回合玩家（Round Robin 的起点）
                memset( gamedata->board,   ' ',  sizeof( gamedata->board)); // 清空棋盘
                
                const char  symbols[]  =  { 'X',  'O',  '#',  '@',   '$'}; // 定义玩家对应的符号
                for( int i=0;  i<gamedata->playercount;   i++)  { // 遍历所有玩家
                    gamedata->players[i].symbol  =  symbols[ i  %  5]; // 为每个玩家分配符号
                }
                
                pthread_mutex_unlock( &gamedata->gamemutex); // 修改完成，释放锁
                printf( "[Game] Starting with %d players!\n",   gamedata->playercount);  fflush( stdout); // 输出启动信息
                addtolog( "SCHEDULER: Game Started!"); // 记录开赛日志
            }  else  { // 未能达到开赛人数
                sleep( 2); // 轮询等待，每 2 秒检查一次
                continue;
            }
        }

        if ( gamedata->gameover)  { // 如果游戏已结束，进入间歇期
            sleep( 1); // 休眠以减轻 CPU 负担
            continue;
        }

        pthread_mutex_lock( &gamedata->gamemutex); // 准备进行轮转调度逻辑
        int  current   =  gamedata->currentturn; // 获取当前轮到的索引
        int  attempts  =  0; // 查找存活玩家的尝试次数
        int  activefound   =  0; // 是否找到了有效的活跃玩家
        
        while ( attempts  <  MAX_PLAYERS)  { // 遍历玩家队列 (Round Robin 算法核心)
            if ( gamedata->players[current].active)  { // 如果该索引对应的玩家仍然在线/活跃
                activefound  =   1; // 标记已找到候选者
                break; // 跳出查找
            }
            current  =  ( current  +  1)   %  gamedata->playercount; // 指向下一个索引（环形队列）
            attempts++; // 尝试次数加一
        }
        
        gamedata->currentturn  =  current; // 更新共享内存中的当前回合索引
        pthread_mutex_unlock( &gamedata->gamemutex); // 释放锁

        if ( !activefound)  { // 如果找了一圈都没人在线了
            resetgame(); // 重置游戏
            continue; // 回到初始化阶段
        }

        sem_post( &gamedata->turnsem[current]); // [调度通知]: 通过信号量通知特定玩家进程可以开始操作了

        sem_wait( &gamedata->schedsem); // [调度挂起]: 调度器调用 sem_wait 等待玩家执行完动作（模拟 OS 等待进程 I/O 或任务完成）

        pthread_mutex_lock( &gamedata->gamemutex); // 玩家动作结束，重新获取锁进行判定
        
        char  playersymbol  =   gamedata->players[current].symbol; // 获取该玩家符号
        if ( checkwin( playersymbol))  { // 调用游戏逻辑检查是否已获胜
            gamedata->winner   =  current; // 记录获胜者 ID
            gamedata->gameover  =  1; // 标记游戏结束
            printf( "\n*** WINNER: %s (Player %d) ***\n\n",   gamedata->players[current].name,  current);  fflush( stdout); // 打印赢家
            addtolog( "GAME: We have a winner!"); // 记录日志
            savescore( gamedata->players[current].name,   1); // 持久化更新分数
        }  else if ( isboardfull())  { // 检查平局
            gamedata->winner  =  -1; // 无获胜者
            gamedata->gameover   =  1; // 标记结束
            printf( "\n*** DRAW - Board is full! ***\n\n");   fflush( stdout); // 打印平局
            addtolog( "GAME: Board full. Draw!"); // 记录日志
        }  else  { // 游戏未结束
            int  nextplayer  =  ( current  +  1)   %  gamedata->playercount; // 计算下一个玩家索引
            gamedata->currentturn  =  nextplayer; // 更新回合 (Round Robin 的推进)
        }
        pthread_mutex_unlock( &gamedata->gamemutex); // 判定完成，释放锁



        if ( gamedata->gameover)  { // 如果判定结果是游戏结束
            usleep( 200000); // 轻微延迟，让判定信息传达

            for ( int i  =  0;   i  <  gamedata->playercount;  i++)  { // 遍历所有玩家
                sem_post( &gamedata->turnsem[i]); // 释放所有人的信号量，解除他们可能存在的阻塞
            }
            printf( "[Scheduler] Game Over! Waiting 5s for clients to finish...\n"); // 提示等待客户端断开
            sleep( 5); // 模拟任务清理周期
            resetgame(); // 重置游戏内存状态
        }
    }
    return  NULL; // 调度器停止
}
// [END: Round Robin Scheduler] (轮转调度器 - 结束)


// [START: IPC & Shared Memory] (进程间通信与共享内存 - 开始)
// [IPC & Shared Memory] - 设置跨进程共享的内存空间
void  setupsharedmemory()  { // 初始化共享内存和同步原语的函数
    shm_unlink( SHM_NAME); // [IPC]: 先移除之前的同名共享内存对象，确保从零开始
    serverfd  =  shm_open( SHM_NAME,   O_CREAT  |  O_RDWR,  0666); // [IPC]: 创建并打开一个新的共享内存段，赋权限 0666
    if ( serverfd   ==  -1)  { // 如果创建失败
        logerror( "setupsharedmemory",   "shm_open failed - cannot create shared memory"); // 记录日志
        exitwitherror( "shm_open"); // 打印致命错误并直接终止程序
    }

    if ( ftruncate( serverfd,   sizeof( GameData))  ==  -1)  { // [IPC]: 设定共享内存对象的大小为结构体 GameData 的尺寸
        logerror( "setupsharedmemory",  "ftruncate failed - cannot resize shared memory"); // 记录日志
        exitwitherror( "ftruncate"); // 终止
    }

    gamedata  =  mmap( NULL,   sizeof( GameData),  PROT_READ  |  PROT_WRITE,  MAP_SHARED,  serverfd,   0); // [Shared Memory]: 将内核中的共享内存段映射到当前进程的虚拟地址空间
    if ( gamedata  ==  MAP_FAILED)  { // 如果内存映射失败
        logerror( "setupsharedmemory",   "mmap failed - cannot map shared memory"); // 记录日志
        exitwitherror( "mmap"); // 终止
    }

    // [START: CrossDomain Synchronization] (跨进程同步 - 开始)
    pthread_mutexattr_t  mutexattr; // [CrossDomain Synch]: 定义互斥锁属性变量
    pthread_mutexattr_init( &mutexattr); // 初始化属性变量
    pthread_mutexattr_setpshared( &mutexattr,   PTHREAD_PROCESS_SHARED); // [非常关键]: 设置互斥锁为“跨进程共享”模式，这样父进程锁子进程也会受限

    pthread_mutex_init( &gamedata->gamemutex,   &mutexattr); // [Synchronization]: 在共享内存中初始化游戏逻辑控制锁
    pthread_mutex_init( &gamedata->logmutex,  &mutexattr); // [Synchronization]: 在共享内存中初始化日志队列访问锁
    
    if ( sem_init( &gamedata->schedsem,  1,   0)  ==  -1)  { // [Synchronization]: 初始化调度器信号量。这里的'1'代表该信号量跨进程共享
        logerror( "setupsharedmemory",   "sem_init for scheduler semaphore failed"); // 记录错误
        exitwitherror( "sem_init sched"); // 终止
    }
    
    for( int i=0;   i<MAX_PLAYERS;  i++)  { // 循环为每个玩家槽位初始化信号量
        if ( sem_init( &gamedata->turnsem[i],   1,  0)  ==  -1)  { // 初始化回合控制信号量，同为跨进程共享模式
            char  errormessage[ 64]; // 错误串缓冲区
            snprintf( errormessage,  64,   "sem_init for turnsem[%d] failed",  i); // 格式化错误
            logerror( "setupsharedmemory",  errormessage); // 记录日志
            exitwitherror( "sem_init turn"); // 终止
        }
    }

    pthread_mutexattr_destroy( &mutexattr); // 销毁不再需要的属性变量
    // [END: CrossDomain Synchronization] (跨进程同步 - 结束)
    
    gamedata->playercount  =  0; // 初始化共享数据：玩家总数清零
    gamedata->connected   =  0; // 初始化共享数据：连接数清零
    gamedata->started  =  0; // 初始化共享数据：游戏标记为未开始
    gamedata->gameover  =  0; // 初始化共享数据：标记未结束
    gamedata->stopflag   =  0; // 初始化共享数据：外部停止标记清除
    memset( gamedata->board,  ' ',   sizeof( gamedata->board)); // 将共享棋盘空间初始化为空格
    
    gamedata->logqueue.head  =  0; // 日志队列头部索引初始化
    gamedata->logqueue.tail   =  0; // 日志队列尾部索引初始化

    printf( "[Server Core] Shared Memory initialized.\n"); // 打印映射与初始化完成的通知
}
// [END: IPC & Shared Memory] (进程间通信与共享内存 - 结束)

void  handleclient( int  socketfd,   int  playerid)  {
    char  buffer[ BUFFER_SIZE];
    
    sleep( 1);
    const char  *welcome  =  "WELCOME\n";
    send( socketfd,   welcome,  strlen( welcome),  0);

    memset( buffer,  0,   BUFFER_SIZE);
    read( socketfd,  buffer,  BUFFER_SIZE);
    
    pthread_mutex_lock( &gamedata->gamemutex);
    strncpy( gamedata->players[playerid].name,   buffer,  31);
    gamedata->players[playerid].active  =  1;
    pthread_mutex_unlock( &gamedata->gamemutex);
    
    printf( "[Server] Player %d joined: %s\n",   playerid,  buffer);  fflush( stdout);
    addtolog( "Player joined");
    
    while( 1)  {
        pthread_mutex_lock( &gamedata->gamemutex);
        int  gamestarted  =  gamedata->started;
        pthread_mutex_unlock( &gamedata->gamemutex);
        
        if ( gamestarted)  {
             send( socketfd,   "START",  5,  0);
             usleep( 100000);
             break;
        }
        sleep( 1);
    }

    while ( 1)  {
        
        struct timespec  timeout;
        clock_gettime( CLOCK_REALTIME,   &timeout);
        timeout.tv_sec  +=  1;

        // [Synchronization]: 玩家进程调用 sem_timedwait 等待调度器(schedulerthread)通过信号量通知
        int  result  =  sem_timedwait( &gamedata->turnsem[playerid],   &timeout); // 阻塞等待自己的回合，超时时间为 1 秒
        
        pthread_mutex_lock( &gamedata->gamemutex); // [Synchronization]: 加锁以读取全局游戏状态变量
        int  isover  =   gamedata->gameover; // 读取游戏是否已结束
        int  winnerid  =  gamedata->winner; // 读取获胜者
        pthread_mutex_unlock( &gamedata->gamemutex); // 解锁
        
        if ( isover)  {
            if ( winnerid   ==  playerid)  {
                printf( "[Game] Player %d (%s) WINS!\n",  playerid,   gamedata->players[playerid].name);  fflush( stdout);
                send( socketfd,  MSG_WIN,   strlen( MSG_WIN),  0);
            }
            else if ( winnerid  ==  -1)  {
                printf( "[Game] Player %d notified of DRAW\n",   playerid);  fflush( stdout);
                send( socketfd,  MSG_DRAW,  strlen( MSG_DRAW),   0);
            }
            else  {
                printf( "[Game] Player %d notified of LOSS\n",  playerid);   fflush( stdout);
                send( socketfd,  MSG_LOSE,   strlen( MSG_LOSE),  0);
            }
            break;
        }
        
        if ( result  ==  0)  {
            pthread_mutex_lock( &gamedata->gamemutex);
            if ( gamedata->gameover)  {
                 int  winnerid   =  gamedata->winner;
                 pthread_mutex_unlock( &gamedata->gamemutex);
                 
                 if ( winnerid  ==  playerid)  {
                     send( socketfd,  MSG_WIN,   strlen( MSG_WIN),  0);
                 }  else if ( winnerid   ==  -1)  {
                     send( socketfd,  MSG_DRAW,  strlen( MSG_DRAW),   0);
                 }  else  {
                     send( socketfd,   MSG_LOSE,  strlen( MSG_LOSE),  0);
                 }
                 break;
            }
            pthread_mutex_unlock( &gamedata->gamemutex);

            send( socketfd,  MSG_YOUR_TURN,   strlen( MSG_YOUR_TURN),  0);
            usleep( 100000);
            char  boardstring[ BOARD_SIZE   *  BOARD_SIZE  +  BOARD_SIZE  +  1];
            int  position  =  0;
            pthread_mutex_lock( &gamedata->gamemutex);
            for( int row=0;  row<BOARD_SIZE;   row++)  {
                for( int col=0;   col<BOARD_SIZE;  col++)  {
                    boardstring[position++]  =  gamedata->board[row][col];
                }
                boardstring[position++]   =  '\n';
            }
            boardstring[position]  =  '\0';
            pthread_mutex_unlock( &gamedata->gamemutex);
            sleep( 1); 
            send( socketfd,  boardstring,   strlen( boardstring),  0);

            int  validmove  =  0;
            int  disconnected   =  0;
            while ( !validmove)  {
                memset( buffer,  0,  BUFFER_SIZE);
                if ( read( socketfd,  buffer,   BUFFER_SIZE)  <=  0)  {
                     char  errormessage[ 128];
                     snprintf( errormessage,   128,  "Client dropped during turn - Player %d (socketfd=%d)",  playerid,  socketfd);
                     logerror( "handleclient",   errormessage);
                     addtolog( "DISCONNECT: Client dropped during turn.");
                     
                     pthread_mutex_lock( &gamedata->gamemutex);
                     gamedata->players[playerid].active   =  0;
                     if ( gamedata->connected  >  0)   gamedata->connected--;
                     pthread_mutex_unlock( &gamedata->gamemutex);
                     
                     sem_post( &gamedata->schedsem); // [Synchronization]: 即使掉线也通知调度器继续，防止死锁
                     disconnected  =  1; // 标记状态为已断开
                     break; // 退出回合循环
                }

                int  row,  col;
                if ( sscanf( buffer,  "%d %d",   &row,  &col)  ==  2)  {
                    pthread_mutex_lock( &gamedata->gamemutex);
                    if ( row  >=  0  &&  row  <  BOARD_SIZE  &&  col  >=  0   &&  col  <  BOARD_SIZE  &&  gamedata->board[row][col]  ==  ' ')  {
                        gamedata->board[row][col]  =  gamedata->players[playerid].symbol;
                        validmove   =  1;
                        char  logmessage[ 64];
                        snprintf( logmessage,  64,  "MOVE: Player %s placed %c at %d,%d",   gamedata->players[playerid].name,  gamedata->players[playerid].symbol,  row,  col);
                        printf( "[Child %d] %s\n",  playerid,   logmessage);  fflush( stdout);
                        pthread_mutex_unlock( &gamedata->gamemutex); 
                        addtolog( logmessage);
                    }  else  {
                        pthread_mutex_unlock( &gamedata->gamemutex);
                    }
                }

                if ( validmove)  send( socketfd,   MSG_VALID_MOVE,  strlen( MSG_VALID_MOVE),  0);
                else  send( socketfd,  MSG_INVALID_MOVE,   strlen( MSG_INVALID_MOVE),  0);
            }

            if ( disconnected)   break;

            // [Synchronization]: 通知调度器(schedulerthread)玩家已完成落子动作
            sem_post( &gamedata->schedsem); 
            
            pthread_mutex_lock( &gamedata->gamemutex);
            if ( checkwin( gamedata->players[playerid].symbol))  {
                 gamedata->winner   =  playerid;
                 gamedata->gameover  =  1;

                 printf( "[Game] Player %d (%s) WINS!\n",   playerid,  gamedata->players[playerid].name);  fflush( stdout);
                 send( socketfd,  MSG_WIN,  strlen( MSG_WIN),   0);
                 
                 pthread_mutex_unlock( &gamedata->gamemutex);
                 break;
            }
            pthread_mutex_unlock( &gamedata->gamemutex);
            continue;
        }  else if ( result  ==  -1  &&   errno  ==  ETIMEDOUT)  {
             pthread_mutex_lock( &gamedata->gamemutex);
             if ( gamedata->gameover)  {
                 int  winnerid  =  gamedata->winner;
                 pthread_mutex_unlock( &gamedata->gamemutex);
                 
                 if ( winnerid   ==  playerid)  {
                     send( socketfd,  MSG_WIN,  strlen( MSG_WIN),   0);
                 }  else if ( winnerid  ==  -1)  {
                     send( socketfd,   MSG_DRAW,  strlen( MSG_DRAW),  0);
                 }  else  {
                     send( socketfd,  MSG_LOSE,   strlen( MSG_LOSE),  0);
                 }
                 break;
             }
             pthread_mutex_unlock( &gamedata->gamemutex);

            continue;
        }

        send( socketfd,  MSG_YOUR_TURN,  strlen( MSG_YOUR_TURN),   0);
        usleep( 100000);
        char  boardstring[ BOARD_SIZE  *  BOARD_SIZE   +  BOARD_SIZE  +  1];
        int  position  =  0;
        pthread_mutex_lock( &gamedata->gamemutex);
        for( int row=0;   row<BOARD_SIZE;  row++)  {
            for( int col=0;  col<BOARD_SIZE;   col++)  {
                boardstring[position++]  =  gamedata->board[row][col];
            }
            boardstring[position++]  =  '\n';
        }
        boardstring[position]   =  '\0';
        pthread_mutex_unlock( &gamedata->gamemutex);
        sleep( 1); 
        send( socketfd,   boardstring,  strlen( boardstring),  0);

        int  validmove  =  0;
        int  disconnected  =  0;
        while ( !validmove)  {
            memset( buffer,  0,   BUFFER_SIZE);
            if ( read( socketfd,  buffer,  BUFFER_SIZE)   <=  0)  {
                 char  errormessage[ 128];
                 snprintf( errormessage,  128,  "Client dropped during turn - Player %d (socketfd=%d)",   playerid,  socketfd);
                 logerror( "handleclient",  errormessage);
                 addtolog( "DISCONNECT: Client dropped during turn.");
                 
                 pthread_mutex_lock( &gamedata->gamemutex);
                 gamedata->players[playerid].active  =  0;
                 if ( gamedata->connected   >  0)  gamedata->connected--;
                 pthread_mutex_unlock( &gamedata->gamemutex);
                 
                 // [Synchronization]: 通知调度器(schedulerthread)玩家已完成落子动作
            sem_post( &gamedata->schedsem); 
                 disconnected   =  1;
                 break;
            }

            if ( strstr( buffer,   "TIMEOUT"))  {
                 printf( "[Child %d] Received Client TIMEOUT signal. Skipping move processing.\n",   playerid);
                 validmove  =  1;
                 continue;
            }

            pthread_mutex_lock( &gamedata->gamemutex);
            int  thisturn  =   gamedata->currentturn;
            pthread_mutex_unlock( &gamedata->gamemutex);

            if ( thisturn  !=  playerid)  {
                 printf( "[Child %d] Move rejected - TIMEOUT (Turn moved to %d)\n",  playerid,   thisturn);
                 send( socketfd,  "*** TIMEOUT! Your turn was skipped. ***\n",   40,  0);
                 validmove  =  1;
                 continue;
            }

            int  row,   col;
            if ( sscanf( buffer,  "%d %d",  &row,   &col)  ==  2)  {
                pthread_mutex_lock( &gamedata->gamemutex);
                if ( row  >=  0  &&  row  <  BOARD_SIZE  &&   col  >=  0  &&  col  <  BOARD_SIZE  &&  gamedata->board[row][col]  ==  ' ')  {
                    gamedata->board[row][col]   =  gamedata->players[playerid].symbol;
                    validmove  =  1;
                    char  logmessage[ 64];
                    snprintf( logmessage,  64,  "MOVE: Player %s placed %c at %d,%d",  gamedata->players[playerid].name,   gamedata->players[playerid].symbol,  row,  col);
                    printf( "[Child %d] %s\n",  playerid,  logmessage);   fflush( stdout);
                    pthread_mutex_unlock( &gamedata->gamemutex); 
                    addtolog( logmessage);
                }  else  {
                    pthread_mutex_unlock( &gamedata->gamemutex);
                }
            }

            if ( validmove)  send( socketfd,  MSG_VALID_MOVE,  strlen( MSG_VALID_MOVE),   0);
            else  send( socketfd,  MSG_INVALID_MOVE,  strlen( MSG_INVALID_MOVE),   0);
        }

        if ( disconnected)  break;

        // [Synchronization]: 通知调度器(schedulerthread)玩家已完成或跳过动作
        sem_post( &gamedata->schedsem);
        
        pthread_mutex_lock( &gamedata->gamemutex);
        if ( checkwin( gamedata->players[playerid].symbol))  {
             gamedata->winner  =  playerid;
             gamedata->gameover   =  1;

             printf( "[Game] Player %d (%s) WINS!\n",  playerid,  gamedata->players[playerid].name);   fflush( stdout);
             send( socketfd,  MSG_WIN,  strlen( MSG_WIN),  0);
             
             pthread_mutex_unlock( &gamedata->gamemutex);
             break;
        }
        pthread_mutex_unlock( &gamedata->gamemutex);
    }
    
    close( socketfd);
    
    pthread_mutex_lock( &gamedata->gamemutex);
    gamedata->players[playerid].active   =  0;
    if ( gamedata->connected  >  0)   gamedata->connected--;
    pthread_mutex_unlock( &gamedata->gamemutex);

    printf( "Child Process for Player %d Exiting. (Connected: %d)\n",   playerid,  gamedata->connected);
    exit( 0);
}

void  signalhandler( int  signal)  {
    if ( signal  ==  SIGINT)  {
        printf( "\n[Server] Shutting down...\n");
        if ( gamedata)  {
            saveallscores();
            gamedata->stopflag   =  1;
            shm_unlink( SHM_NAME);
        }
        exit( 0);
    }
    if ( signal   ==  SIGCHLD)  {
        while( waitpid( -1,  NULL,  WNOHANG)   >  0);
    }
}

int  main( int  argc,  char  *argv[])  {
    signal( SIGINT,   signalhandler);
    signal( SIGCHLD,  signalhandler);
    
    srand( time( NULL));

    if ( argc  >  1)  {
        port  =  atoi( argv[1]);
    }

    printf( "[Server] Starting Mega Tic-Tac-Toe Server on port %d...\n",   port);

    setupsharedmemory();
    loadscores();

// [START: Multithreading (pthreads)] (多线程处理 - 开始)
    pthread_t  logthread,   schedthread; // [Multithreading]: 在栈上定义两个线程描述符（TID 变量）
    pthread_create( &logthread,  NULL,   loggerthread,  NULL); // [Multithreading]: 创建日志线程，执行 loggerthread 函数
    pthread_create( &schedthread,  NULL,  schedulerthread,   NULL); // [Multithreading]: 创建调度线程，执行 schedulerthread 函数
// [END: Multithreading (pthreads)] (多线程处理 - 结束)

    int  listenfd,  newsocket;
    struct sockaddr_in  serveraddr;
    int  addrlen   =  sizeof( serveraddr);

    if ( ( listenfd  =  socket( AF_INET,  SOCK_STREAM,   0))  ==  0)  {
        logerror( "main",   "socket() failed - cannot create listening socket");
        exitwitherror( "socket failed");
    }
    
    int  option  =  1;
    if ( setsockopt( listenfd,  SOL_SOCKET,  SO_REUSEADDR,   &option,  sizeof( option)))  {
        logerror( "main",  "setsockopt() failed - cannot set socket options");
        exitwitherror( "setsockopt");
    }

    serveraddr.sin_family  =  AF_INET;
    serveraddr.sin_addr.s_addr   =  INADDR_ANY;
    serveraddr.sin_port  =  htons( port);

    if ( bind( listenfd,  ( struct sockaddr  *)&serveraddr,   sizeof( serveraddr))  <  0)  {
        char  errormessage[ 64];
        snprintf( errormessage,  64,  "bind() failed on port %d - Address may be in use",   port);
        logerror( "main",  errormessage);
        exitwitherror( "bind failed");
    }
    if ( listen( listenfd,  MAX_PLAYERS)   <  0)  {
        logerror( "main",   "listen() failed - cannot start listening");
        exitwitherror( "listen");
    }

    printf( "[Server] Waiting for connections...\n");

    while ( 1)  {
        if ( ( newsocket  =  accept( listenfd,  ( struct sockaddr  *)&serveraddr,   ( socklen_t*)&addrlen))  <  0)  {
           if ( errno  ==  EINTR)   continue;
           perror( "accept");
           continue;
        }

        pthread_mutex_lock( &gamedata->gamemutex);
        
        int  connectedcount  =  gamedata->connected;
        
        if ( connectedcount  <  MAX_PLAYERS   &&  !gamedata->started)  {
             int  id  =  gamedata->playercount;
             if ( gamedata->playercount  <  MAX_PLAYERS)  {
                 gamedata->playercount++;
             }  else  {
                 int  freeslot   =  -1;
                 for( int i=0;  i<MAX_PLAYERS;   i++)  {
                     if ( !gamedata->players[i].active)  {
                         freeslot  =  i;
                         break;
                     }
                 }
                 id   =  freeslot;
             }

             if ( id  !=  -1)  {
                 gamedata->players[id].active   =  1;
                 gamedata->connected++;
                 pthread_mutex_unlock( &gamedata->gamemutex);

// [START: Multiprocessing (fork)] (多进程处理 - 开始)
                 pid_t  childpid  =  fork(); // [Multiprocessing]: 调用 fork() 创建一个全新的子进程
                 if ( childpid   ==  0)  { // 如果返回 0，说明当前代码运行在“子进程”中
                     close( listenfd); // 子进程不需要监听 socket，关闭它
                     handleclient( newsocket,  id); // 子进程专门负责处理该客户端的通信
                     exit( 0); // 处理完毕后，子进程退出
                 }  else if ( childpid  <  0)  { // 如果返回负数，说明创建子进程失败
                     logerror( "main",   "fork() failed - cannot create child process for client");
                     perror( "Fork failed");
                 }  else  { // 如果返回正数，说明当前代码运行在“父进程”中，返回值为子进程 PID
                     close( newsocket); // 父进程不需要与该客户端通信，关闭 socket 交给子进程
                     printf( "[Server Debug] Parent: Closed socket for Child %d, returning to Accept loop.\n",   id);
                     fflush( stdout);
                 }
// [END: Multiprocessing (fork)] (多进程处理 - 结束)
             }  else  {
                 pthread_mutex_unlock( &gamedata->gamemutex);
                 close( newsocket);
                 printf( "[Server] Rejected connection: Full.\n");
             }
        }  else  {
             pthread_mutex_unlock( &gamedata->gamemutex);
             close( newsocket);
             printf( "[Server] Rejected connection: Game in progress or Full.\n");
        }
    }

    return  0;
}
