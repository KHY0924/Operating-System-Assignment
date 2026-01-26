#include "common.h"

/******************************************************************************
 * TEAM MEMBER IMPLEMENTATION STATUS
 * 
 * Member 1 (Server Core/IPC): IMPLEMENTED
 * Member 2 (Scheduler/Game):  IMPLEMENTED
 * Member 3 (Logger):          IMPLEMENTED
 * Member 4 (Client/Scores):   IMPLEMENTED
 ******************************************************************************/

// --- Global Variables ---
SharedState *shm_ptr;
int server_fd;
int port = PORT;

// --- Helper Functions ---
void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// ============================================================================
// >>> MEMBER 3 IMPLEMENTATION: CONCURRENT LOGGER <<<
// ============================================================================

void enqueue_log(const char *msg) {
    if (!shm_ptr) return;

    // Get current time
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", t);

    // Format message with timestamp
    char formattedMsg[LOG_MSG_LEN];
    snprintf(formattedMsg, LOG_MSG_LEN, "[%s] %s", timeStr, msg);

    pthread_mutex_lock(&shm_ptr->log_mutex);
    
    // Check if space available (Circular Buffer)
    int next_head = (shm_ptr->log_queue.head + 1) % LOG_QUEUE_SIZE;
    if (next_head != shm_ptr->log_queue.tail) {
        strncpy(shm_ptr->log_queue.messages[shm_ptr->log_queue.head], formattedMsg, LOG_MSG_LEN);
        shm_ptr->log_queue.head = next_head;
        shm_ptr->log_queue.count++;
    } else {
        // Drop message or overwrite (Dropping for safety prevent overflow logic complication)
    }

    pthread_mutex_unlock(&shm_ptr->log_mutex);
}

void *logger_thread_func(void *arg) {
    printf("[Logger Thread] Started.\n");
    FILE *logFile = fopen("game.log", "a");
    if (!logFile) {
        perror("Failed to open game.log");
        return NULL;
    }

    // Unbuffer to write immediately
    setbuf(logFile, NULL);

    while (!shm_ptr->stop_threads) {
        int worked = 0;
        
        pthread_mutex_lock(&shm_ptr->log_mutex);
        if (shm_ptr->log_queue.tail != shm_ptr->log_queue.head) {
            // Dequeue
            fprintf(logFile, "%s\n", shm_ptr->log_queue.messages[shm_ptr->log_queue.tail]);
            fflush(logFile); // Ensure real-time update in game.log
            shm_ptr->log_queue.tail = (shm_ptr->log_queue.tail + 1) % LOG_QUEUE_SIZE;
            shm_ptr->log_queue.count--;
            worked = 1;
        }
        pthread_mutex_unlock(&shm_ptr->log_mutex);

        if (!worked) {
            // Sleep to reduce CPU usage if queue is empty
            usleep(50000); // 50ms
        }
    }

    fclose(logFile);
    printf("[Logger Thread] Stopped.\n");
    return NULL;
}

// ============================================================================
// >>> MEMBER 4 IMPLEMENTATION REVISITED: PERSISTENCE <<<
// ============================================================================

void load_scores() {
    if (!shm_ptr) return;

    pthread_mutex_lock(&shm_ptr->game_mutex);
    shm_ptr->num_scores = 0;
    FILE *fp = fopen("scores.txt", "r");
    if (!fp) {
        fp = fopen("scores.txt", "w"); // Create if missing
        if(fp) fclose(fp);
        pthread_mutex_unlock(&shm_ptr->game_mutex);
        return;
    }

    char name[32];
    int wins;
    while(fscanf(fp, "%s %d", name, &wins) == 2 && shm_ptr->num_scores < 100) {
        strncpy(shm_ptr->high_scores[shm_ptr->num_scores].name, name, 31);
        shm_ptr->high_scores[shm_ptr->num_scores].wins = wins;
        shm_ptr->num_scores++;
    }
    fclose(fp);
    pthread_mutex_unlock(&shm_ptr->game_mutex);
    
    char logMsg[100];
    snprintf(logMsg, 100, "PERSISTENCE: Loaded %d scores.", shm_ptr->num_scores);
    enqueue_log(logMsg);
}

void save_score(const char *player_name, int add_win) {
    if (!shm_ptr || !player_name) return;

    // NOTE: Mutex Removed here to prevent deadlock (Caller must hold lock!)

    int found = 0;
    for (int i = 0; i < shm_ptr->num_scores; i++) {
        if (strcmp(shm_ptr->high_scores[i].name, player_name) == 0) {
            shm_ptr->high_scores[i].wins += add_win;
            found = 1;
            break;
        }
    }

    if (!found && shm_ptr->num_scores < 100) {
        strncpy(shm_ptr->high_scores[shm_ptr->num_scores].name, player_name, 31);
        shm_ptr->high_scores[shm_ptr->num_scores].wins = add_win;
        shm_ptr->num_scores++;
    }

    if (shm_ptr->num_scores == 0) {
        printf("[Score Debug] Warning: No scores to save (num_scores=0). Skipping write to prevent data loss.\n");
        return;
    }

    FILE *fp = fopen("scores.txt", "w");
    if (fp) {
        for (int i = 0; i < shm_ptr->num_scores; i++) {
            fprintf(fp, "%s %d\n", shm_ptr->high_scores[i].name, shm_ptr->high_scores[i].wins);
        }
        fclose(fp);
        printf("[Score Debug] Successfully wrote %d scores to scores.txt\n", shm_ptr->num_scores);
    } else {
        perror("[Score Debug] Failed to open scores.txt for writing");
    }
    
    char logMsg[128];
    snprintf(logMsg, 128, "PERSISTENCE: Saved score for %s. Total scores in memory: %d", player_name, shm_ptr->num_scores);
    enqueue_log(logMsg);
}

// Function to save all scores (used on shutdown)
void save_all_scores() {
    if (!shm_ptr) return;
    pthread_mutex_lock(&shm_ptr->game_mutex);
    FILE *fp = fopen("scores.txt", "w");
    if (fp) {
        for (int i = 0; i < shm_ptr->num_scores; i++) {
            fprintf(fp, "%s %d\n", shm_ptr->high_scores[i].name, shm_ptr->high_scores[i].wins);
        }
        fclose(fp);
    }
    pthread_mutex_unlock(&shm_ptr->game_mutex);
}


// ============================================================================
// >>> MEMBER 2 IMPLEMENTATION: SCHEDULER & GAME LOGIC <<<
// ============================================================================

// Check board for N consecutive symbols. Return 1 if win, 0 otherwise.
int check_win(char symbol) {
    // Horizontal
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c <= BOARD_SIZE - WIN_LEN; c++) {
            int count = 0;
            for (int k = 0; k < WIN_LEN; k++) {
                if (shm_ptr->board[r][c+k] == symbol) count++;
            }
            if (count == WIN_LEN) return 1;
        }
    }
    // Vertical
    for (int c = 0; c < BOARD_SIZE; c++) {
        for (int r = 0; r <= BOARD_SIZE - WIN_LEN; r++) {
            int count = 0;
            for (int k = 0; k < WIN_LEN; k++) {
                if (shm_ptr->board[r+k][c] == symbol) count++;
            }
            if (count == WIN_LEN) return 1;
        }
    }
    // Diagonal (\)
    for (int r = 0; r <= BOARD_SIZE - WIN_LEN; r++) {
        for (int c = 0; c <= BOARD_SIZE - WIN_LEN; c++) {
            int count = 0;
            for (int k = 0; k < WIN_LEN; k++) {
                if (shm_ptr->board[r+k][c+k] == symbol) count++;
            }
            if (count == WIN_LEN) return 1;
        }
    }
    // Diagonal (/)
    for (int r = 0; r <= BOARD_SIZE - WIN_LEN; r++) {
        for (int c = WIN_LEN - 1; c < BOARD_SIZE; c++) {
            int count = 0;
            for (int k = 0; k < WIN_LEN; k++) {
                if (shm_ptr->board[r+k][c-k] == symbol) count++;
            }
            if (count == WIN_LEN) return 1;
        }
    }
    return 0;
}

int is_board_full() {
    for(int i=0; i<BOARD_SIZE; i++)
        for(int j=0; j<BOARD_SIZE; j++)
            if(shm_ptr->board[i][j] == ' ') return 0;
    return 1;
}

void reset_game() {
    pthread_mutex_lock(&shm_ptr->game_mutex);
    memset(shm_ptr->board, ' ', sizeof(shm_ptr->board));
    shm_ptr->game_started = 0;
    shm_ptr->game_over = 0;
    shm_ptr->winner_id = -1;
    pthread_mutex_unlock(&shm_ptr->game_mutex);
    enqueue_log("GAME: Board reset.");
}

void *scheduler_thread_func(void *arg) {
    printf("[Scheduler Thread] Started.\n");

    while(!shm_ptr->stop_threads) {
        
        // 1. Wait for Enough Players
        pthread_mutex_lock(&shm_ptr->game_mutex);
        int connected = shm_ptr->num_connected;
        int started = shm_ptr->game_started;
        pthread_mutex_unlock(&shm_ptr->game_mutex);

        if (!started) {
            // Wait for minimum players
            if (connected >= MIN_PLAYERS) {
                // Try to start game if not started
                // FIX: Give more time for 4th/5th players to join!
                if (connected < MAX_PLAYERS) {
                    printf("[Scheduler] Minimum players met. Waiting 15s for others to join...\n");
                    enqueue_log("SCHEDULER: Minimum players met. Waiting 15s for others...");
                    sleep(15); 
                } else {
                    enqueue_log("SCHEDULER: Max players reached. Starting immediately!");
                } 
                
                pthread_mutex_lock(&shm_ptr->game_mutex);
                shm_ptr->game_started = 1;
                shm_ptr->game_over = 0;
                shm_ptr->winner_id = -1;
                shm_ptr->turn_index = 0; // Start with first player
                memset(shm_ptr->board, ' ', sizeof(shm_ptr->board));
                
                // Assign Symbols
                const char symbols[] = {'X', 'O', '#', '@', '$'};
                for(int i=0; i<shm_ptr->num_players; i++) {
                    shm_ptr->players[i].symbol = symbols[i % 5];
                }
                
                pthread_mutex_unlock(&shm_ptr->game_mutex);
                printf("[Game] Starting with %d players!\n", shm_ptr->num_players); fflush(stdout);
                enqueue_log("SCHEDULER: Game Started!");
            } else {
                sleep(2); // Wait for players
                continue;
            }
        }

        // 2. Round Robin Turn Loop
        if (shm_ptr->game_over) {
            // Wait for reset or cleanup
            sleep(1);
            continue;
        }

        pthread_mutex_lock(&shm_ptr->game_mutex);
        int current = shm_ptr->turn_index;
        // Skip inactive players
        int attempts = 0;
        int active_found = 0;
        
        // Ensure we check at least once to find an active player
        while (attempts < MAX_PLAYERS) {
            // printf("[Scheduler Debug] Checking Player %d Active: %d\n", current, shm_ptr->players[current].active); fflush(stdout);
            if (shm_ptr->players[current].active) {
                active_found = 1;
                break;
            }
            current = (current + 1) % shm_ptr->num_players;
            attempts++;
        }
        
        shm_ptr->turn_index = current;
        pthread_mutex_unlock(&shm_ptr->game_mutex);

        if (!active_found) {
            // No active players? Reset.
            reset_game();
            continue;
        }

        // Signal Player to Move
        sem_post(&shm_ptr->turn_sem[current]);

        // Wait for Move Completion (Infinite Wait)
        sem_wait(&shm_ptr->sched_sem);

        // Check Win/Draw conditions after move
        pthread_mutex_lock(&shm_ptr->game_mutex);
        
        // Refresh Current Player (in case logic changed? Unlikely but safe)
        char sym = shm_ptr->players[current].symbol;
        if (check_win(sym)) {
            shm_ptr->winner_id = current;
            shm_ptr->game_over = 1;
            printf("\n*** WINNER: %s (Player %d) ***\n\n", shm_ptr->players[current].name, current); fflush(stdout);
            enqueue_log("GAME: We have a winner!");
            save_score(shm_ptr->players[current].name, 1);
        } else if (is_board_full()) {
             shm_ptr->winner_id = -1; // Draw
             shm_ptr->game_over = 1;
             printf("\n*** DRAW - Board is full! ***\n\n"); fflush(stdout);
             enqueue_log("GAME: Board full. Draw!");
        } else {
             // Next turn
             int next = (current + 1) % shm_ptr->num_players;
             shm_ptr->turn_index = next;
        }
        pthread_mutex_unlock(&shm_ptr->game_mutex);



        if (shm_ptr->game_over) {
            // FIX: Add delay to prevent "WIN" message sticking to "VALID" message
            usleep(200000); // 0.2 seconds delay

            // Signal all players to wake up and check game_over
            for (int i = 0; i < shm_ptr->num_players; i++) {
                sem_post(&shm_ptr->turn_sem[i]);
            }
            // Give clients time to receive and display the WIN/LOSE message
            printf("[Scheduler] Game Over! Waiting 5s for clients to finish...\n");
            sleep(5);
            reset_game();
        }
    }
    return NULL;
}


// ============================================================================
// >>> MEMBER 1 IMPLEMENTATION: SERVER CORE & CHILD PROCESS <<<
// ============================================================================

void setup_shared_memory() {
    shm_unlink(SHM_NAME);
    server_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (server_fd == -1) error_exit("shm_open");

    if (ftruncate(server_fd, sizeof(SharedState)) == -1) error_exit("ftruncate");

    shm_ptr = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, server_fd, 0);
    if (shm_ptr == MAP_FAILED) error_exit("mmap");

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shm_ptr->game_mutex, &attr);
    pthread_mutex_init(&shm_ptr->log_mutex, &attr);
    
    if (sem_init(&shm_ptr->sched_sem, 1, 0) == -1) error_exit("sem_init sched");
    
    for(int i=0; i<MAX_PLAYERS; i++) {
        if (sem_init(&shm_ptr->turn_sem[i], 1, 0) == -1) error_exit("sem_init turn");
    }

    pthread_mutexattr_destroy(&attr);
    
    // Init state
    shm_ptr->num_players = 0;
    shm_ptr->num_connected = 0;
    shm_ptr->game_started = 0;
    shm_ptr->game_over = 0;
    shm_ptr->stop_threads = 0;
    memset(shm_ptr->board, ' ', sizeof(shm_ptr->board));
    
    // Init Log
    shm_ptr->log_queue.head = 0;
    shm_ptr->log_queue.tail = 0;

    printf("[Server Core] Shared Memory initialized.\n");
}

void handle_client(int socket_fd, int player_id) {
    char buffer[BUFFER_SIZE];
    
    // 1. Handshake
    sleep(1); // Give client a moment
    const char *welcome = "WELCOME\n";
    send(socket_fd, welcome, strlen(welcome), 0);

    // 2. Receive Name
    memset(buffer, 0, BUFFER_SIZE);
    read(socket_fd, buffer, BUFFER_SIZE);
    
    pthread_mutex_lock(&shm_ptr->game_mutex);
    strncpy(shm_ptr->players[player_id].name, buffer, 31);
    shm_ptr->players[player_id].active = 1;
    pthread_mutex_unlock(&shm_ptr->game_mutex);
    
    printf("[Server] Player %d joined: %s\n", player_id, buffer); fflush(stdout);
    enqueue_log("Player joined");
    
    // 3. Wait for Game Start
    while(1) {
        pthread_mutex_lock(&shm_ptr->game_mutex);
        int start = shm_ptr->game_started;
        pthread_mutex_unlock(&shm_ptr->game_mutex);
        
        if (start) {
             send(socket_fd, "START", 5, 0);
             usleep(100000); // 100ms delay to prevent TCP concatenation
             break;
        }
        sleep(1);
    }

    // 4. Game Loop
    while (1) {
        
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // 1 second timeout for polling

        // printf("[Child %d Debug] Waiting for turn semaphore...\n", player_id); fflush(stdout);
        int result = sem_timedwait(&shm_ptr->turn_sem[player_id], &ts);
        
        // First, check if game is over (whether woken by signal or timeout)
        pthread_mutex_lock(&shm_ptr->game_mutex);
        int over = shm_ptr->game_over;
        int winner = shm_ptr->winner_id;
        pthread_mutex_unlock(&shm_ptr->game_mutex);
        
        if (over) {
            if (winner == player_id) {
                // printf("[Game Debug] Sending WIN to Player %d...\n", player_id); fflush(stdout);
                printf("[Game] Player %d (%s) WINS!\n", player_id, shm_ptr->players[player_id].name); fflush(stdout);
                send(socket_fd, MSG_WIN, strlen(MSG_WIN), 0);
            }
            else if (winner == -1) {
                printf("[Game] Player %d notified of DRAW\n", player_id); fflush(stdout);
                send(socket_fd, MSG_DRAW, strlen(MSG_DRAW), 0);
            }
            else {
                printf("[Game] Player %d notified of LOSS\n", player_id); fflush(stdout);
                send(socket_fd, MSG_LOSE, strlen(MSG_LOSE), 0);
            }
            break;
        }
        
        if (result == 0) {
            // It's our turn OR Game Over signal from scheduler
            
            // FIX: Check Game Over even if we got the semaphore!
            pthread_mutex_lock(&shm_ptr->game_mutex);
            if (shm_ptr->game_over) {
                 int winner = shm_ptr->winner_id;
                 pthread_mutex_unlock(&shm_ptr->game_mutex);
                 
                 // Send corresponding message
                 if (winner == player_id) {
                     send(socket_fd, MSG_WIN, strlen(MSG_WIN), 0);
                 } else if (winner == -1) {
                     send(socket_fd, MSG_DRAW, strlen(MSG_DRAW), 0);
                 } else {
                     send(socket_fd, MSG_LOSE, strlen(MSG_LOSE), 0);
                 }
                 break; // Exit loop
            }
            pthread_mutex_unlock(&shm_ptr->game_mutex);

            // If not game over, proceed to send YOUR_TURN
            
            // It IS our turn!
            send(socket_fd, MSG_YOUR_TURN, strlen(MSG_YOUR_TURN), 0);
            usleep(100000); // 100ms delay before sending board
            // Send Board
            char boardStr[BOARD_SIZE * BOARD_SIZE + BOARD_SIZE + 1]; // Rows + newlines
            int pos = 0;
            pthread_mutex_lock(&shm_ptr->game_mutex);
            for(int r=0; r<BOARD_SIZE; r++) {
                for(int c=0; c<BOARD_SIZE; c++) {
                    boardStr[pos++] = shm_ptr->board[r][c];
                }
                boardStr[pos++] = '\n';
            }
            boardStr[pos] = '\0';
            pthread_mutex_unlock(&shm_ptr->game_mutex);
            sleep(1); 
            send(socket_fd, boardStr, strlen(boardStr), 0);

            // Receive Move
            int valid = 0;
            int dropped = 0;
            while (!valid) {
                memset(buffer, 0, BUFFER_SIZE);
                if (read(socket_fd, buffer, BUFFER_SIZE) <= 0) {
                     enqueue_log("DISCONNECT: Client dropped during turn.");
                     
                     // Restore Drop Logic
                     pthread_mutex_lock(&shm_ptr->game_mutex);
                     shm_ptr->players[player_id].active = 0;
                     if (shm_ptr->num_connected > 0) shm_ptr->num_connected--; // Decrement connected count
                     pthread_mutex_unlock(&shm_ptr->game_mutex);
                     
                     sem_post(&shm_ptr->sched_sem); // Release scheduler
                     dropped = 1;
                     break;
                }

                // r/c parsing logic...
                int r, c;
                if (sscanf(buffer, "%d %d", &r, &c) == 2) {
                    pthread_mutex_lock(&shm_ptr->game_mutex);
                    if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE && shm_ptr->board[r][c] == ' ') {
                        shm_ptr->board[r][c] = shm_ptr->players[player_id].symbol;
                        valid = 1;
                        char moveLog[64];
                        snprintf(moveLog, 64, "MOVE: Player %s placed %c at %d,%d", shm_ptr->players[player_id].name, shm_ptr->players[player_id].symbol, r, c);
                        printf("[Child %d] %s\n", player_id, moveLog); fflush(stdout);
                        pthread_mutex_unlock(&shm_ptr->game_mutex); 
                        enqueue_log(moveLog);
                    } else {
                        pthread_mutex_unlock(&shm_ptr->game_mutex);
                    }
                }

                if (valid) send(socket_fd, MSG_VALID_MOVE, strlen(MSG_VALID_MOVE), 0);
                else send(socket_fd, MSG_INVALID_MOVE, strlen(MSG_INVALID_MOVE), 0);
            }

            if (dropped) break; // Exit loop if dropped

            // Notify Scheduler that move is done
            sem_post(&shm_ptr->sched_sem);
            
            // FIX: Proactive Win Check - Do not wait for scheduler!
            pthread_mutex_lock(&shm_ptr->game_mutex);
            if (check_win(shm_ptr->players[player_id].symbol)) {
                 // printf("[Game Debug] Child %d detected logic WIN proactively!\n", player_id); fflush(stdout);
                 
                 // We won! Set the state so scheduler knows too
                 shm_ptr->winner_id = player_id;
                 shm_ptr->game_over = 1;

                 // printf("[Game Debug] Sending WIN to Player %d...\n", player_id); fflush(stdout);
                 printf("[Game] Player %d (%s) WINS!\n", player_id, shm_ptr->players[player_id].name); fflush(stdout);
                 send(socket_fd, MSG_WIN, strlen(MSG_WIN), 0);
                 
                 pthread_mutex_unlock(&shm_ptr->game_mutex);
                 break; // Exit loop immediately
            }
            pthread_mutex_unlock(&shm_ptr->game_mutex);
            
            // FIX: Continue to wait for next turn, don't fall through!
            continue;
        } else if (result == -1 && errno == ETIMEDOUT) {
             // TIMEOUT Case:
             // 1. Check Game Over FIRST (Crucial for losers waiting)
             pthread_mutex_lock(&shm_ptr->game_mutex);
             if (shm_ptr->game_over) {
                 int winner = shm_ptr->winner_id;
                 pthread_mutex_unlock(&shm_ptr->game_mutex);
                 
                 // Send corresponding message
                 if (winner == player_id) {
                     send(socket_fd, MSG_WIN, strlen(MSG_WIN), 0);
                 } else if (winner == -1) {
                     send(socket_fd, MSG_DRAW, strlen(MSG_DRAW), 0);
                 } else {
                     send(socket_fd, MSG_LOSE, strlen(MSG_LOSE), 0);
                 }
                 break; // Exit loop
             }
             pthread_mutex_unlock(&shm_ptr->game_mutex);

            // Timeout: Send PING... (Commented out)
            /* 
            if (send(socket_fd, "PING", 4, MSG_NOSIGNAL) == -1) {
                 enqueue_log("DISCONNECT: Client lost.");
                 pthread_mutex_lock(&shm_ptr->game_mutex);
                 shm_ptr->players[player_id].active = 0;
                 pthread_mutex_unlock(&shm_ptr->game_mutex);
                 break;
            }
            */
            continue;
        } else {
            // Error case - unexpected sem_timedwait result
            continue;
        }

        // NOTE: The code below should NEVER be reached because all cases above
        // either 'break', 'continue', or are enclosed in their own handling.
        // If we reach here, it IS our turn!
        send(socket_fd, MSG_YOUR_TURN, strlen(MSG_YOUR_TURN), 0);
        usleep(100000); // 100ms delay before sending board
        // Send Board
        char boardStr[BOARD_SIZE * BOARD_SIZE + BOARD_SIZE + 1]; // Rows + newlines
        int pos = 0;
        pthread_mutex_lock(&shm_ptr->game_mutex);
        for(int r=0; r<BOARD_SIZE; r++) {
            for(int c=0; c<BOARD_SIZE; c++) {
                boardStr[pos++] = shm_ptr->board[r][c];
            }
            boardStr[pos++] = '\n';
        }
        boardStr[pos] = '\0';
        pthread_mutex_unlock(&shm_ptr->game_mutex);
        sleep(1); 
        send(socket_fd, boardStr, strlen(boardStr), 0);

        // Receive Move
        int valid = 0;
        int dropped = 0;
        while (!valid) {
            memset(buffer, 0, BUFFER_SIZE);
            if (read(socket_fd, buffer, BUFFER_SIZE) <= 0) {
                 enqueue_log("DISCONNECT: Client dropped during turn.");
                 
                 // Restore Drop Logic
                 pthread_mutex_lock(&shm_ptr->game_mutex);
                 shm_ptr->players[player_id].active = 0;
                 if (shm_ptr->num_connected > 0) shm_ptr->num_connected--; // Decrement connected count
                 pthread_mutex_unlock(&shm_ptr->game_mutex);
                 
                 sem_post(&shm_ptr->sched_sem); // Release scheduler
                 dropped = 1;
                 break;
            }

            // FIX: Handle Client-Reported TIMEOUT
            if (strstr(buffer, "TIMEOUT")) {
                 printf("[Child %d] Received Client TIMEOUT signal. Skipping move processing.\n", player_id);
                 valid = 1; // Break input loop
                 // Do not post sched_sem here, let the scheduler loop handle turn change
                 continue; // Continue to end of loop/next iteration
            }

            // FIX: Check if we timed out (Scheduler Moved On?)
            pthread_mutex_lock(&shm_ptr->game_mutex);
            int current_turn = shm_ptr->turn_index;
            pthread_mutex_unlock(&shm_ptr->game_mutex);

            if (current_turn != player_id) {
                 printf("[Child %d] Move rejected - TIMEOUT (Turn moved to %d)\n", player_id, current_turn);
                 send(socket_fd, "*** TIMEOUT! Your turn was skipped. ***\n", 40, 0);
                 valid = 1; // Break input loop but don't process move logic
                 // Do NOT post sched_sem because scheduler already continued
                 continue; // Loop back to semaphore wait (wait for next legitimate turn)
            }
            // End FIX

            int r, c;
            if (sscanf(buffer, "%d %d", &r, &c) == 2) {
                pthread_mutex_lock(&shm_ptr->game_mutex);
                if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE && shm_ptr->board[r][c] == ' ') {
                    shm_ptr->board[r][c] = shm_ptr->players[player_id].symbol;
                    valid = 1;
                    char moveLog[64];
                    snprintf(moveLog, 64, "MOVE: Player %s placed %c at %d,%d", shm_ptr->players[player_id].name, shm_ptr->players[player_id].symbol, r, c);
                    printf("[Child %d] %s\n", player_id, moveLog); fflush(stdout);
                    pthread_mutex_unlock(&shm_ptr->game_mutex); 
                    enqueue_log(moveLog);
                } else {
                    pthread_mutex_unlock(&shm_ptr->game_mutex);
                }
            }

            if (valid) send(socket_fd, MSG_VALID_MOVE, strlen(MSG_VALID_MOVE), 0);
            else send(socket_fd, MSG_INVALID_MOVE, strlen(MSG_INVALID_MOVE), 0);
        }

        if (dropped) break; // Exit loop if dropped

        // Notify Scheduler that move is done
        sem_post(&shm_ptr->sched_sem);
        
        // FIX: Proactive Win Check - Do not wait for scheduler!
        pthread_mutex_lock(&shm_ptr->game_mutex);
        if (check_win(shm_ptr->players[player_id].symbol)) {
             // printf("[Game Debug] Child %d detected logic WIN proactively!\n", player_id); fflush(stdout);
             
             // We won! Set the state so scheduler knows too
             shm_ptr->winner_id = player_id;
             shm_ptr->game_over = 1;

             printf("[Game] Player %d (%s) WINS!\n", player_id, shm_ptr->players[player_id].name); fflush(stdout);
             send(socket_fd, MSG_WIN, strlen(MSG_WIN), 0);
             
             pthread_mutex_unlock(&shm_ptr->game_mutex);
             break; // Exit loop immediately
        }
        pthread_mutex_unlock(&shm_ptr->game_mutex);
    }
    
    close(socket_fd);
    
    // FIX: Clean up player state on normal exit so new players can join
    pthread_mutex_lock(&shm_ptr->game_mutex);
    shm_ptr->players[player_id].active = 0;
    if (shm_ptr->num_connected > 0) shm_ptr->num_connected--;
    pthread_mutex_unlock(&shm_ptr->game_mutex);

    printf("Child Process for Player %d Exiting. (Connected: %d)\n", player_id, shm_ptr->num_connected);
    exit(0);
}

void signal_handler(int sig) {
    if (sig == SIGINT) {
        printf("\n[Server] Shutting down...\n");
        if (shm_ptr) {
            save_all_scores(); // Requirement 7.1
            shm_ptr->stop_threads = 1;
            shm_unlink(SHM_NAME);
        }
        exit(0);
    }
    if (sig == SIGCHLD) {
        while(waitpid(-1, NULL, WNOHANG) > 0);
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGCHLD, signal_handler);
    
    srand(time(NULL));

    // If port argument provided
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("[Server] Starting Mega Tic-Tac-Toe Server on port %d...\n", port);

    // 1. Setup Shared Memory
    setup_shared_memory();
    load_scores();

    // 2. Start Request Threads
    pthread_t log_tid, sched_tid;
    pthread_create(&log_tid, NULL, logger_thread_func, NULL);
    pthread_create(&sched_tid, NULL, scheduler_thread_func, NULL);

    // 3. Network Setup
    int listen_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) error_exit("socket failed");
    
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) error_exit("setsockopt");

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) error_exit("bind failed");
    if (listen(listen_fd, MAX_PLAYERS) < 0) error_exit("listen");

    printf("[Server] Waiting for connections...\n");

    while (1) {
        if ((new_socket = accept(listen_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
           if (errno == EINTR) continue; // interrupted by signal
           perror("accept");
           continue;
        }

        pthread_mutex_lock(&shm_ptr->game_mutex);
        
        int connected = shm_ptr->num_connected;
        
        // We only allow joining if we have space and game hasn't started yet?
        // Or can we join late? Strategy: No join after start.
        if (connected < MAX_PLAYERS && !shm_ptr->game_started) {
             int id = shm_ptr->num_players;
             if (shm_ptr->num_players < MAX_PLAYERS) {
                 shm_ptr->num_players++;
             } else {
                 // Trying to reconnect to a finished game slot?
                 // Complex logic. Simplified: reset resets num_players?
                 // If reset resets num_players, then id increments are fine.
                 // We will assume num_players grows until MAX and then we check active.
                 int free_slot = -1;
                 for(int i=0; i<MAX_PLAYERS; i++) {
                     if (!shm_ptr->players[i].active) {
                         free_slot = i;
                         break;
                     }
                 }
                 id = free_slot;
             }

             if (id != -1) {
                 shm_ptr->players[id].active = 1;
                 shm_ptr->num_connected++;
                 pthread_mutex_unlock(&shm_ptr->game_mutex);

                 pid_t pid = fork();
                 if (pid == 0) {
                     // Child
                     close(listen_fd);
                     handle_client(new_socket, id);
                     exit(0);
                 } else if (pid < 0) {
                     perror("Fork failed");
                 } else {
                     // Parent
                     close(new_socket);
                     printf("[Server Debug] Parent: Closed socket for Child %d, returning to Accept loop.\n", id);
                     fflush(stdout);
                 }
             } else {
                 pthread_mutex_unlock(&shm_ptr->game_mutex);
                 close(new_socket);
                 printf("[Server] Rejected connection: Full.\n");
             }
        } else {
             pthread_mutex_unlock(&shm_ptr->game_mutex);
             close(new_socket);
             printf("[Server] Rejected connection: Game in progress or Full.\n");
        }
    }

    return 0;
}
