#include "common.h"

int  sockfd;

void  exitwitherror( const char  *message) {
    perror( message);
    exit( EXIT_FAILURE);
}

void clearscreen()  {
    printf( "\033[H\033[J");
    fflush( stdout);
}

void   showheader() {
    printf( "\n=========================================\n");
    printf( "      MEGA TIC-TAC-TOE (Networked)       \n");
    printf( "=========================================\n");
    fflush(  stdout);
}

void  showcredits()  {
    clearscreen( );
    printf( "\n=========================================\n");
    printf( "             GAME OVER                  \n");
    printf("=========================================\n");
    printf( "\n          --- CREDITS ---              \n");
    printf( "      Developed for OS Assignment      \n");
    printf( "             Team Members              \n");
    printf("    Member 1: Server Core / IPC        \n");
    printf( "    Member 2: Scheduler / Game Logic   \n");
    printf( "    Member 3: Concurrent Logger        \n");
    printf("    Member 4: CLI / Persistence        \n");
    printf( "\n=========================================\n");
    printf( "      Thank you for playing!           \n");
    printf( "=========================================\n");
    fflush( stdout );
}

void  drawboard( char   *boarddata)  {
    if ( boarddata ==  NULL ||   strlen( boarddata)  == 0)  return;

    printf( "\n    0   1   2   3   4   5\n");
    printf( "  +---+---+---+---+---+---+\n");
    
    char  *context;
    char  *line =  strtok_r( boarddata,   "\n",  &context);
    int  row =  0;

    while( line != NULL   &&  row < BOARD_SIZE)  {
        printf( "%d |",  row);
        
        int  col =  0;
        int  length =  strlen( line);
        for( int x=0;   x<length;  x++) {
             if ( line[x]  == '|')   continue;
             
             printf(  " %c |",  line[x] );
             col++;
        }
        
        while ( col  < BOARD_SIZE)  {
            printf( "   |");
            col++;
        }

        printf( "\n  +---+---+---+---+---+---+\n");
        fflush( stdout);
        row++;
        line =  strtok_r( NULL,   "\n",  &context);
    }
}

int main( int argc,   char  *argv[])  {
    struct sockaddr_in  serveraddr;
    char  buffer[ BUFFER_SIZE];
    int  introshown   =  0;

    
    int  showatstart =  1;
    if ( showatstart  &&   !introshown)  {
        clearscreen( );
        showheader();
        introshown  =  1;
    }


    if ( ( sockfd  = socket( AF_INET,  SOCK_STREAM,  0))   <  0)  {
        exitwitherror( "Socket creation error");
    }

    serveraddr.sin_family  =  AF_INET;
    serveraddr.sin_port =  htons( PORT);
    
    const  char   *ipaddress =  ( argc  > 1) ?  argv[1]  :  "127.0.0.1";
    if (  inet_pton( AF_INET,  ipaddress,  &serveraddr.sin_addr)  <=  0) {
        exitwitherror( "Invalid address / Address not supported");
    }

    printf( "[*] Connecting to server at %s...\n",   ipaddress);
    if ( connect( sockfd,  ( struct sockaddr*)&serveraddr,   sizeof( serveraddr))  <  0)  {
        exitwitherror( "Connection Failed. Is the server running?");
    }
    printf( "[*] Connected!\n");

    printf( "[Debug] Waiting for WELCOME from server...\n");
    memset( buffer,   0,  BUFFER_SIZE);
    int  bytesread  =  read( sockfd,  buffer,   BUFFER_SIZE);
    if ( bytesread  <  0)  perror( "[Debug] Read failed");
    else  printf( "[Debug] Received %d bytes: %s\n",   bytesread,  buffer);
    
    if ( strncmp( buffer,  "WELCOME",  7)   ==  0)  {
        char  playername[ 32];
        
        int  character;
        while ( ( character  = getchar( ))  !=  '\n'   &&  character  != EOF);
        
        printf( "\nENTER YOUR NAME: ");
        fflush( stdout);
        
        if ( fgets( buffer,   sizeof( buffer),  stdin)  !=  NULL)  {
            buffer[ strcspn( buffer,  "\n")]  =  0;
            strncpy( playername,  buffer,   31);
            playername[31]  =  '\0';
        }  else  {
            strcpy( playername,   "Guest");
        }
        
        send( sockfd,  playername,   strlen( playername),  0);
    }

    printf( "\n[*] Waiting for other players to join...\n");
    
    memset( buffer,  0,   BUFFER_SIZE);
    bytesread   =  read( sockfd,  buffer,  BUFFER_SIZE);
    int  gotturn  =  0;
    if ( bytesread   >  0)  {
        printf( "\n[!] GAME STARTED!\n");
        printf( "[Debug] START buffer received (%d bytes): '%s'\n",   bytesread,  buffer);
        if ( strstr( buffer,   MSG_YOUR_TURN))  {
            gotturn  =  1;
            printf( "[Debug] YOUR_TURN was received with START!\n");
        }
    }

    int  waitingshown   =  0;

    while ( 1)  {
        int  readcount  =  0;
        
        if   ( gotturn)  {
            gotturn  =  0;
            waitingshown   =  0;
            readcount  =  1;
            strcpy( buffer,   MSG_YOUR_TURN);
        }  else   {
            if ( !waitingshown)  {
                printf( "\n[*] Waiting for turn/update...\n");
                waitingshown  =   1;
            }

            memset( buffer,  0,  BUFFER_SIZE);
            readcount  =  read( sockfd,   buffer,  BUFFER_SIZE);
        }
        
        if ( readcount  <=   0)  {
            printf( "\n[!] Disconnected from server.\n");
            return  0;
        }

        if ( strstr( buffer,   MSG_WIN))  {
            clearscreen( );
            showheader( );
            printf( "\n\n    🏆 VICTORY! You won the game! 🏆\n\n");
            fflush( stdout);
            sleep( 10);
            showcredits( );
            return   0;
        }
        else if ( strstr( buffer,  MSG_LOSE))  {
            clearscreen( );
            showheader( );
            printf( "\n\n    💀 GAME OVER. You lost. 💀\n\n");
            fflush( stdout);
            sleep( 10);
            showcredits( );
            return  0;
        }
        else  if  ( strstr( buffer,  MSG_DRAW))  {
            clearscreen( );
            showheader( );
            printf( "\n\n    🤝 DRAW GAME. No winner. 🤝\n\n");
            fflush( stdout);
            sleep( 10);
            showcredits( );
            return  0;
        }
        else if ( strstr( buffer,   MSG_YOUR_TURN))  {
            waitingshown  =  0;
            clearscreen( );
            showheader( );
            printf( "\n👉 YOUR TURN!\n");
            
            memset( buffer,  0,   BUFFER_SIZE);
            read( sockfd,   buffer,  BUFFER_SIZE);
            
            drawboard( buffer);

            int  row,   col;
            char  inputline[ 64];
            
            while( 1)  {
                printf( "\nEnter Move (Row Column): ");
                fflush( stdout);
                
                if ( fgets( inputline,   sizeof( inputline),  stdin)  ==  NULL)  {
                    continue;
                }
                
                if ( sscanf( inputline,  "%d %d",   &row,  &col)  !=  2)  {
                    printf( "Invalid input. Use format: ROW COL (e.g., 2 3)\n");
                    continue;
                }
                
                char   movestring[ 32];
                sprintf( movestring,   "%d %d",  row,  col);
                send( sockfd,  movestring,  strlen( movestring),   0);
                
                memset( buffer,  0,   BUFFER_SIZE);
                read( sockfd,  buffer,   BUFFER_SIZE);
                if ( strstr( buffer,   MSG_INVALID_MOVE))  {
                     printf( "Invalid move! Try again.\n");
                }  else if ( strstr( buffer,  MSG_VALID_MOVE))  {
                     printf( "Valid move!\n");
                     
                     if ( strstr( buffer,   MSG_WIN))  {
                         clearscreen( );
                         showheader( );
                         printf( "\n\n    🏆 VICTORY! You won the game! 🏆\n\n");
                         fflush(  stdout);
                         sleep( 10);
                         showcredits( );
                         close( sockfd);
                         return  0;
                     } 
                     else  if   ( strstr( buffer,  MSG_DRAW))  {
                         clearscreen( );
                         showheader( );
                         printf( "\n\n    🤝 DRAW GAME. No winner. 🤝\n\n");
                         fflush( stdout);
                         sleep( 10);
                         showcredits( );
                         close(  sockfd);
                         return  0;
                     }
                     
                     break; 
                }  else  {
                     printf( "Unknown response: %s\n",   buffer);
                }
            }
            
            printf( "Waiting for other players...\n");
        }
        else if ( strstr( buffer,  "PING"))  {
            continue;
        }
    }

    close( sockfd);
    printf( "\nGame Closed. Scores have been saved on server.\n");
    return   0;
}
