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
    if (boardStr == NULL || strlen(boardStr) == 0) return;

    // The server sends the board as a single string, likely row by row.
    // Example expectation: " | | | | | \n | | | | | \n..." 
    // We will clean it up and print with borders.

    printf("\n    0   1   2   3   4   5\n");
    printf("  +---+---+---+---+---+---+\n");
    
    char *line_ctx;
    char *line = strtok_r(boardStr, "\n", &line_ctx); // Use thread-safe strtok_r if available, or just strtok
    int row = 0;

    while(line != NULL && row < BOARD_SIZE) {
        printf("%d |", row);
        
        // Iterate through the line string. 
        // Expected format per cell: "C|" where C is 'X', 'O' or ' '
        // We will just print characters that are not separators nicely.
        
        int col = 0;
        int len = strlen(line);
        for(int i=0; i<len; i++) {
             // Skip existing pipes in the string to draw our own consistent ones
             if (line[i] == '|') continue;
             
             printf(" %c |", line[i]);
             col++;
        }
        
        // Fill remaining columns if line was short (safety)
        while (col < BOARD_SIZE) {
            printf("   |");
            col++;
        }

        printf("\n  +---+---+---+---+---+---+\n");
        row++;
        line = strtok_r(NULL, "\n", &line_ctx);
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
    printf("[Debug] Waiting for WELCOME from server...\n");
    memset(buffer, 0, BUFFER_SIZE);
    int n = read(sock_fd, buffer, BUFFER_SIZE);
    if (n < 0) perror("[Debug] Read failed");
    else printf("[Debug] Received %d bytes: %s\n", n, buffer);
    // Server says "WELCOME"
    
    // 2. Send Name
    if (strncmp(buffer, "WELCOME", 7) == 0) {
        char name[32];
        printf("\nENTER YOUR NAME: ");
    scanf("%s", name);
    send(sock_fd, name, strlen(name), 0);
    } // End of WELCOME check

    printf("\n[*] Waiting for other players to join...\n");
    
    // Wait for START
    memset(buffer, 0, BUFFER_SIZE);
    n = read(sock_fd, buffer, BUFFER_SIZE); // Block until START
    int your_turn_in_start = 0;  // Flag if YOUR_TURN came with START
    if (n > 0) {
        printf("\n[!] GAME STARTED!\n");
        printf("[Debug] START buffer received (%d bytes): '%s'\n", n, buffer);
        // Check if YOUR_TURN was concatenated with START
        if (strstr(buffer, MSG_YOUR_TURN)) {
            your_turn_in_start = 1;
            printf("[Debug] YOUR_TURN was received with START!\n");
        }
    }

    int waiting_message_shown = 0;

    while (1) {
        int valread = 0;
        
        // If YOUR_TURN came with START, skip reading and process it
        if (your_turn_in_start) {
            your_turn_in_start = 0;  // Clear flag
            waiting_message_shown = 0;
            // Buffer already contains YOUR_TURN from the START read
            // We need to fake valread > 0 to enter the processing
            valread = 1;  // Non-zero to pass the check
            // Buffer still has content from the START message
            strcpy(buffer, MSG_YOUR_TURN);  // Set buffer to YOUR_TURN for processing
        } else {
            if (!waiting_message_shown) {
                printf("\n[*] Waiting for turn/update...\n");
                waiting_message_shown = 1;
            }

            memset(buffer, 0, BUFFER_SIZE);
            valread = read(sock_fd, buffer, BUFFER_SIZE);
            
            // DEBUG: Print EVERYTHING received
            /*
            if (valread > 0) {
                printf("[DEBUG RAW] Received %d bytes: '%s'\n", valread, buffer);
            }
            */
        }
        
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
            waiting_message_shown = 0; // Reset for next time
            clear_screen();
            print_header();
            printf("\n👉 YOUR TURN!\n");
            
            // Get Board
            memset(buffer, 0, BUFFER_SIZE);
            read(sock_fd, buffer, BUFFER_SIZE);
            
            // Render Board
            print_board_pretty(buffer);

            // Input Loop
            // Input Loop
            int r, c;
            char inputLine[64];
            
            while(1) {
                printf("\nEnter Move (Row Column): ");
                fflush(stdout);
                
                // Use fgets for more robust input handling
                if (fgets(inputLine, sizeof(inputLine), stdin) == NULL) {
                    continue;
                }
                
                // Parse the input line
                if (sscanf(inputLine, "%d %d", &r, &c) != 2) {
                    printf("Invalid input. Use format: ROW COL (e.g., 2 3)\n");
                    continue;
                }
                
                // Send Move
                char moveStr[32];
                sprintf(moveStr, "%d %d", r, c);
                send(sock_fd, moveStr, strlen(moveStr), 0);
                
                // Wait for Validity Response
                memset(buffer, 0, BUFFER_SIZE);
                read(sock_fd, buffer, BUFFER_SIZE);
                // Check INVALID first since "INVALID" contains "VALID" as substring!
                if (strstr(buffer, MSG_INVALID_MOVE)) {
                     printf("Invalid move! Try again.\n");
                } else if (strstr(buffer, MSG_VALID_MOVE)) {
                     printf("Valid move!\n");
                     
                     // FIX: Check if Game Over message came attached with VALID
                     if (strstr(buffer, MSG_WIN)) {
                         clear_screen();
                         print_header();
                         printf("\n\n    🏆 VICTORY! You won the game! 🏆\n\n");
                         close(sock_fd);
                         return 0;
                     } 
                     else if (strstr(buffer, MSG_DRAW)) {
                         printf("\n\n    🤝 DRAW GAME. No winner. 🤝\n\n");
                         close(sock_fd);
                         return 0;
                     }
                     
                     break; 
                } else {
                     printf("Unknown response: %s\n", buffer);
                }
            }
            
            printf("Waiting for other players...\n");
        }
        else if (strstr(buffer, "PING")) {
            // Keep-alive message, ignore
            continue;
        }
    }

    close(sock_fd);
    printf("\nGame Closed. Scores have been saved on server.\n");
    return 0;
}
