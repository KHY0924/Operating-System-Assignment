#include "common.h"

// --- Member 4: Client Implementation ---
// Responsibilities: CLI Interface, Connection, Protocol Handling

int sock_fd;

void error_exit_client(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void clear_screen() {
    // Standard ANSI escape sequence to clear screen
    printf("\033[H\033[J");
}

void print_header() {
    printf("\n=========================================\n");
    printf("      MEGA TIC-TAC-TOE (Networked)       \n");
    printf("=========================================\n");
}

void print_board_pretty(char *boardStr) {
    printf("\n    0   1   2   3   4   5\n");
    printf("  +---+---+---+---+---+---+\n");
    
    int row = 0;
    char *line = strtok(boardStr, "\n");
    while(line != NULL) {
        printf("%d |", row);
        
        // The server sends "X| | |O|...". We parse it.
        // Or simpler, just print what server sends if it's pre-formatted?
        // Our server sends crude "X| |". Let's parse or just print elegantly.
        // Server sends: "c|c|c|c|c|c|"
        
        for(int i=0; i<strlen(line); i++) {
            if(line[i] == '|') printf(" %c |", line[i-1]); 
            // This relies on specific server format "C|"
        }
        // Correct approach: Just print char by char based on server msg structure
        // But for robustness, let's assume server sends "Row\n"
        // Let's just trust the server's visual format for now, but Member 4 should parse.
        // Since I wrote the server to send "C|C|...", let's assume we just display it.
        
        // Actually, let's interpret the string for better UI
        // Server format: "C|C|C|C|C|C|\n" repeated 6 times.
        
        int col = 0;
        int len = strlen(line);
        for(int i=0; i<len; i++) {
             if (line[i] != '|') {
                 char c = line[i];
                 if (c == ' ') printf("   |");
                 else printf(" %c |", c);
                 col++;
             }
        }
        
        printf("\n  +---+---+---+---+---+---+\n");
        row++;
        line = strtok(NULL, "\n");
    }
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    
    // UI: Introduction
    clear_screen();
    print_header();

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        error_exit_client("Socket creation error");
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // Connect to localhost by default, or argv[1]
    const char *target_ip = (argc > 1) ? argv[1] : "127.0.0.1";
    if (inet_pton(AF_INET, target_ip, &serv_addr.sin_addr) <= 0) {
        error_exit_client("Invalid address / Address not supported");
    }

    printf("[*] Connecting to server at %s...\n", target_ip);
    if (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        error_exit_client("Connection Failed. Is the server running?");
    }
    printf("[*] Connected!\n");

    // 1. Handshake
    memset(buffer, 0, BUFFER_SIZE);
    read(sock_fd, buffer, BUFFER_SIZE);
    // Server says "WELCOME"
    
    // 2. Send Name
    char name[32];
    printf("\nENTER YOUR NAME: ");
    scanf("%s", name);
    send(sock_fd, name, strlen(name), 0);

    printf("\n[*] Waiting for other players to join...\n");
    
    // Wait for START
    memset(buffer, 0, BUFFER_SIZE);
    int n = read(sock_fd, buffer, BUFFER_SIZE); // Block until START
    if (n > 0) printf("\n[!] GAME STARTED!\n");

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int valread = read(sock_fd, buffer, BUFFER_SIZE);
        if (valread <= 0) {
            printf("\n[!] Disconnected from server.\n");
            break;
        }

        // Protocol Handling
        if (strstr(buffer, MSG_WIN)) {
            clear_screen();
            print_header();
            printf("\n\n    🏆 VICTORY! You won the game! 🏆\n\n");
            break;
        }
        else if (strstr(buffer, MSG_LOSE)) {
            printf("\n\n    💀 GAME OVER. You lost. 💀\n\n");
            break;
        }
        else if (strstr(buffer, MSG_DRAW)) {
            printf("\n\n    🤝 DRAW GAME. No winner. 🤝\n\n");
            break;
        }
        else if (strstr(buffer, MSG_YOUR_TURN)) {
            clear_screen();
            print_header();
            printf("\n👉 YOUR TURN!\n");
            
            // Get Board
            memset(buffer, 0, BUFFER_SIZE);
            read(sock_fd, buffer, BUFFER_SIZE);
            
            // Render Board
            print_board_pretty(buffer);

            // Input Loop
            int r, c;
            while(1) {
                printf("\nEnter Move (Row Column): ");
                if (scanf("%d %d", &r, &c) != 2) {
                    while(getchar() != '\n'); // flush stdin
                    printf("Invalid input. Use format: ROW COL (e.g., 2 3)\n");
                    continue;
                }
                
                // Send Move
                char move[32];
                snprintf(move, 32, "%d %d", r, c);
                send(sock_fd, move, strlen(move), 0);
                
                // Wait for Validity Response
                memset(buffer, 0, BUFFER_SIZE);
                read(sock_fd, buffer, BUFFER_SIZE);
                if (strstr(buffer, MSG_VALID_MOVE)) {
                     printf("move accepted...\n");
                     break; 
                } else {
                     printf("❌ Invalid move! Spot taken or out of bounds. Try again.\n");
                }
            }
            
            printf("Waiting for other players...\n");
        }
    }

    close(sock_fd);
    printf("\nGame Closed. Scores have been saved on server.\n");
    return 0;
}
