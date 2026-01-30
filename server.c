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

void  addtolog( const char  *message)  {
    if ( !gamedata)   return;

    time_t  now  =   time( NULL);
    struct tm  *timeinfo  =  localtime( &now);
    char  timestring[ 64];
    strftime( timestring,  sizeof( timestring),   "%Y-%m-%d %H:%M:%S",  timeinfo);

    char  fullmessage[ LOG_MSG_LEN];
    snprintf( fullmessage,   LOG_MSG_LEN,  "[%s] %s",  timestring,  message);

    pthread_mutex_lock( &gamedata->logmutex);
    
    int  nexthead  =   ( gamedata->logqueue.head  +  1)  %  LOG_QUEUE_SIZE;
    if ( nexthead  !=   gamedata->logqueue.tail)  {
        strncpy( gamedata->logqueue.messages[ gamedata->logqueue.head],   fullmessage,  LOG_MSG_LEN);
        gamedata->logqueue.head  =  nexthead;
        gamedata->logqueue.count++;
    }

    pthread_mutex_unlock( &gamedata->logmutex);
}

void  *loggerthread( void   *arg)  {
    printf( "[Logger Thread] Started.\n");
    FILE  *logfile  =  fopen( "game.log",   "a");
    if ( !logfile)  {
        logerror( "loggerthread",   "Failed to open game.log for writing");
        perror( "Failed to open game.log");
        return  NULL;
    }

    setbuf( logfile,   NULL);

    while ( !gamedata->stopflag)  {
        int  didwork   =  0;
        
        pthread_mutex_lock( &gamedata->logmutex);
        if ( gamedata->logqueue.tail   !=  gamedata->logqueue.head)  {
            fprintf( logfile,  "%s\n",   gamedata->logqueue.messages[ gamedata->logqueue.tail]);
            fflush( logfile);
            gamedata->logqueue.tail  =   ( gamedata->logqueue.tail  +  1)  %  LOG_QUEUE_SIZE;
            gamedata->logqueue.count--;
            didwork  =  1;
        }
        pthread_mutex_unlock( &gamedata->logmutex);

        if ( !didwork)  {
            usleep( 50000);
        }
    }

    fclose( logfile);
    printf( "[Logger Thread] Stopped.\n");
    return   NULL;
}

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

void  resetgame()  {
    pthread_mutex_lock( &gamedata->gamemutex);
    memset( gamedata->board,  ' ',   sizeof( gamedata->board));
    gamedata->started  =  0;
    gamedata->gameover   =  0;
    gamedata->winner  =  -1;
    pthread_mutex_unlock( &gamedata->gamemutex);
    addtolog( "GAME: Board reset.");
}

void  *schedulerthread( void  *arg)  {
    printf( "[Scheduler Thread] Started.\n");

    while( !gamedata->stopflag)  {
        
        pthread_mutex_lock( &gamedata->gamemutex);
        int  connectedcount  =   gamedata->connected;
        int  gamestarted  =  gamedata->started;
        pthread_mutex_unlock( &gamedata->gamemutex);

        if ( !gamestarted)  {
            if ( connectedcount  >=   MIN_PLAYERS)  {
                if ( connectedcount   <  MAX_PLAYERS)  {
                    printf( "[Scheduler] Minimum players met. Waiting 15s for others to join...\n");
                    addtolog( "SCHEDULER: Minimum players met. Waiting 15s for others...");
                    sleep( 15); 
                }  else  {
                    addtolog( "SCHEDULER: Max players reached. Starting immediately!");
                } 
                
                pthread_mutex_lock( &gamedata->gamemutex);
                gamedata->started   =  1;
                gamedata->gameover  =  0;
                gamedata->winner  =  -1;
                gamedata->currentturn   =  0;
                memset( gamedata->board,   ' ',  sizeof( gamedata->board));
                
                const char  symbols[]  =  { 'X',  'O',  '#',  '@',   '$'};
                for( int i=0;  i<gamedata->playercount;   i++)  {
                    gamedata->players[i].symbol  =  symbols[ i  %  5];
                }
                
                pthread_mutex_unlock( &gamedata->gamemutex);
                printf( "[Game] Starting with %d players!\n",   gamedata->playercount);  fflush( stdout);
                addtolog( "SCHEDULER: Game Started!");
            }  else  {
                sleep( 2);
                continue;
            }
        }

        if ( gamedata->gameover)  {
            sleep( 1);
            continue;
        }

        pthread_mutex_lock( &gamedata->gamemutex);
        int  current   =  gamedata->currentturn;
        int  attempts  =  0;
        int  activefound   =  0;
        
        while ( attempts  <  MAX_PLAYERS)  {
            if ( gamedata->players[current].active)  {
                activefound  =   1;
                break;
            }
            current  =  ( current  +  1)   %  gamedata->playercount;
            attempts++;
        }
        
        gamedata->currentturn  =  current;
        pthread_mutex_unlock( &gamedata->gamemutex);

        if ( !activefound)  {
            resetgame();
            continue;
        }

        sem_post( &gamedata->turnsem[current]);

        sem_wait( &gamedata->schedsem);

        pthread_mutex_lock( &gamedata->gamemutex);
        
        char  playersymbol  =   gamedata->players[current].symbol;
        if ( checkwin( playersymbol))  {
            gamedata->winner   =  current;
            gamedata->gameover  =  1;
            printf( "\n*** WINNER: %s (Player %d) ***\n\n",   gamedata->players[current].name,  current);  fflush( stdout);
            addtolog( "GAME: We have a winner!");
            savescore( gamedata->players[current].name,   1);
        }  else if ( isboardfull())  {
             gamedata->winner  =  -1;
             gamedata->gameover   =  1;
             printf( "\n*** DRAW - Board is full! ***\n\n");   fflush( stdout);
             addtolog( "GAME: Board full. Draw!");
        }  else  {
             int  nextplayer  =  ( current  +  1)   %  gamedata->playercount;
             gamedata->currentturn  =  nextplayer;
        }
        pthread_mutex_unlock( &gamedata->gamemutex);



        if ( gamedata->gameover)  {
            usleep( 200000);

            for ( int i  =  0;   i  <  gamedata->playercount;  i++)  {
                sem_post( &gamedata->turnsem[i]);
            }
            printf( "[Scheduler] Game Over! Waiting 5s for clients to finish...\n");
            sleep( 5);
            resetgame();
        }
    }
    return  NULL;
}


void  setupsharedmemory()  {
    shm_unlink( SHM_NAME);
    serverfd  =  shm_open( SHM_NAME,   O_CREAT  |  O_RDWR,  0666);
    if ( serverfd   ==  -1)  {
        logerror( "setupsharedmemory",   "shm_open failed - cannot create shared memory");
        exitwitherror( "shm_open");
    }

    if ( ftruncate( serverfd,   sizeof( GameData))  ==  -1)  {
        logerror( "setupsharedmemory",  "ftruncate failed - cannot resize shared memory");
        exitwitherror( "ftruncate");
    }

    gamedata  =  mmap( NULL,   sizeof( GameData),  PROT_READ  |  PROT_WRITE,  MAP_SHARED,  serverfd,   0);
    if ( gamedata  ==  MAP_FAILED)  {
        logerror( "setupsharedmemory",   "mmap failed - cannot map shared memory");
        exitwitherror( "mmap");
    }

    pthread_mutexattr_t  mutexattr;
    pthread_mutexattr_init( &mutexattr);
    pthread_mutexattr_setpshared( &mutexattr,   PTHREAD_PROCESS_SHARED);

    pthread_mutex_init( &gamedata->gamemutex,   &mutexattr);
    pthread_mutex_init( &gamedata->logmutex,  &mutexattr);
    
    if ( sem_init( &gamedata->schedsem,  1,   0)  ==  -1)  {
        logerror( "setupsharedmemory",   "sem_init for scheduler semaphore failed");
        exitwitherror( "sem_init sched");
    }
    
    for( int i=0;   i<MAX_PLAYERS;  i++)  {
        if ( sem_init( &gamedata->turnsem[i],   1,  0)  ==  -1)  {
            char  errormessage[ 64];
            snprintf( errormessage,  64,   "sem_init for turnsem[%d] failed",  i);
            logerror( "setupsharedmemory",  errormessage);
            exitwitherror( "sem_init turn");
        }
    }

    pthread_mutexattr_destroy( &mutexattr);
    
    gamedata->playercount  =  0;
    gamedata->connected   =  0;
    gamedata->started  =  0;
    gamedata->gameover  =  0;
    gamedata->stopflag   =  0;
    memset( gamedata->board,  ' ',   sizeof( gamedata->board));
    
    gamedata->logqueue.head  =  0;
    gamedata->logqueue.tail   =  0;

    printf( "[Server Core] Shared Memory initialized.\n");
}

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

        int  result  =  sem_timedwait( &gamedata->turnsem[playerid],   &timeout);
        
        pthread_mutex_lock( &gamedata->gamemutex);
        int  isover  =   gamedata->gameover;
        int  winnerid  =  gamedata->winner;
        pthread_mutex_unlock( &gamedata->gamemutex);
        
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
                     
                     sem_post( &gamedata->schedsem);
                     disconnected  =  1;
                     break;
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

    pthread_t  logthread,   schedthread;
    pthread_create( &logthread,  NULL,   loggerthread,  NULL);
    pthread_create( &schedthread,  NULL,  schedulerthread,   NULL);

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

                 pid_t  childpid  =  fork();
                 if ( childpid   ==  0)  {
                     close( listenfd);
                     handleclient( newsocket,  id);
                     exit( 0);
                 }  else if ( childpid  <  0)  {
                     logerror( "main",   "fork() failed - cannot create child process for client");
                     perror( "Fork failed");
                 }  else  {
                     close( newsocket);
                     printf( "[Server Debug] Parent: Closed socket for Child %d, returning to Accept loop.\n",   id);
                     fflush( stdout);
                 }
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
