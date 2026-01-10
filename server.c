#include "common.h"

/******************************************************************************
 * TEAM MEMBER IMPLEMENTATION STATUS
 * 
 * Member 1 (Server Core/IPC): PENDING
 * Member 2 (Scheduler/Game):  PENDING
 * Member 3 (Logger):          PENDING
 * Member 4 (Client/Scores):   IMPLEMENTED (Partial in server.c)
 ******************************************************************************/

// --- Global Variables (To be managed by Member 1) ---
SharedState *shm_ptr;
int server_fd;

// --- Helper Functions ---
void error_exit(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// --- Placeholder for Member 3 (Logger) ---
void enqueue_log(const char *msg) {
    // TODO: Member 3 to implement safe logging to shared memory
    printf("[LOG MOCK]: %s\n", msg);
}

// ============================================================================
// >>> MEMBER 4 IMPLEMENTATION START: PERSISTENCE <<<
// Responsibilities: Manage scores.txt, atomic updates, loading/saving.
// ============================================================================

void load_scores() {
    FILE *fp = fopen("scores.txt", "r");
    if (!fp) {
        // Create if doesn't exist
        fp = fopen("scores.txt", "w");
        if (fp) {
             fprintf(fp, "Server_Start 0\n"); // Init header
             fclose(fp);
             enqueue_log("PERSISTENCE: Created new scores.txt database.");
        }
        return;
    }

    char logMsg[100];
    int count = 0;
    char name[32];
    int wins;
    
    while(fscanf(fp, "%s %d", name, &wins) == 2) {
        count++;
    }
    fclose(fp);
    
    snprintf(logMsg, sizeof(logMsg), "PERSISTENCE: Loaded scores.txt (%d records).", count);
    enqueue_log(logMsg);
}

void save_score(const char *player_name, int add_win) {
    // Strictly uses standard file I/O to persist data.
    // Called by Scheduler (Member 2) when a game ends.
    
    // 1. Read all existing scores into memory
    struct Record { char name[32]; int wins; };
    struct Record records[MAX_PLAYERS * 10]; // Buffer limits
    int count = 0;
    
    FILE *fp = fopen("scores.txt", "r");
    if (fp) {
        while (count < (MAX_PLAYERS * 10) && fscanf(fp, "%s %d", records[count].name, &records[count].wins) == 2) {
            count++;
        }
        fclose(fp);
    }

    // 2. Update or Add the winner
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(records[i].name, player_name) == 0) {
            if (add_win) records[i].wins++;
            found = 1;
            break;
        }
    }
    
    if (!found && count < (MAX_PLAYERS * 10)) {
        strncpy(records[count].name, player_name, 31);
        records[count].wins = add_win ? 1 : 0;
        count++;
    }

    // 3. Write back (Truncate)
    fp = fopen("scores.txt", "w");
    if (!fp) {
        perror("Failed to write scores.txt");
        return;
    }
    
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%s %d\n", records[i].name, records[i].wins);
    }
    fclose(fp);

    char logMsg[100];
    snprintf(logMsg, 100, "PERSISTENCE: Score updated for %s. Database saved.", player_name);
    // Note: enqueue_log is Member 3's function, we just call it.
    enqueue_log(logMsg);
}
// ============================================================================
// >>> MEMBER 4 IMPLEMENTATION END <<<
// ============================================================================


// --- Member 3: Concurrent Logger ---
void *logger_thread_func(void *arg) {
    // TODO: Member 3 Implementation (Loop, read log_queue, write to file)
    return NULL;
}

// --- Member 2: Game Logic & Scheduler ---
void *scheduler_thread_func(void *arg) {
    // TODO: Member 2 Implementation (Round Robin, Valid Move Check, Win Check)
    // IMPORTANT: When game ends, Member 2 MUST call Member 4's save_score() function.
    return NULL;
}

// --- Member 1: Server Core ---
void setup_shared_memory() {
    // TODO: Member 1 Implementation (shm_open, mmap, mutex init)
}

void signal_handler(int sig) {
    // TODO: Member 1 Implementation (SIGINT cleanup, SIGCHLD reaping)
    if (sig == SIGINT) {
        printf("\n[Server Stub] Shutting down (Saving scores...)\n");
        // We can call Member 4's logic here too if needed
        exit(0);
    }
}

int main() {
    signal(SIGINT, signal_handler);
    
    printf("[Server] Starting up...\n");
    printf("[Server] Note: Only Member 4 (Persistence) logic is implemented.\n");

    // Member 1 Responsibility
    setup_shared_memory();
    
    // Member 4 Responsibility
    load_scores();

    // Start Threads (M2 & M3)
    // pthread_create...

    // Main Loop (Accept & Fork) (M1)
    // while(1) ...

    printf("[Server] Waiting for Team Member 1, 2, 3 implementations to run.\n");
    while(1) sleep(10); 

    return 0;
}
