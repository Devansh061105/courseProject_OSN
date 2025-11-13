#include "ss_server.h"
#include "../common/utils.h"
#include "../common/logger.h"
#include "../common/error_codes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static StorageServerState g_state;
static volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
    g_state.running = false;
}

int main(int argc, char* argv[]) {
    if (argc < 7) {
        printf("Usage: %s SS_ID BASE_PATH NM_IP NM_PORT CLIENT_PORT SS_PORT\n", argv[0]);
        printf("Example: %s 1 ./data/ss1 127.0.0.1 8000 9001 9101\n", argv[0]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    int ss_id = atoi(argv[1]);
    int nm_port = atoi(argv[4]);
    int client_port = atoi(argv[5]);
    int ss_port = atoi(argv[6]);
    
    printf("\nInitializing Storage Server %d...\n", ss_id);
    
    if (ss_init(&g_state, ss_id, argv[2], argv[3], nm_port, client_port, ss_port) < 0) {
        fprintf(stderr, "Failed to initialize storage server\n");
        return 1;
    }
    
    printf("Scanning files from %s...\n", argv[2]);
    int file_count = scan_and_register_files(&g_state);
    printf("Registered %d files\n", file_count);
    
    printf("Connecting to Name Server at %s:%d...\n", argv[3], nm_port);
    if (register_with_name_server(&g_state) < 0) {
        fprintf(stderr, "Failed to register with Name Server\n");
        ss_cleanup(&g_state);
        return 1;
    }
    
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   Docs++ Storage Server v1.0           ║\n");
    printf("╚════════════════════════════════════════╝\n\n");
    printf("Storage Server %d running on:\n", ss_id);
    printf("  Client Port: %d\n", client_port);
    printf("  SS Port: %d\n", ss_port);
    printf("  Files: %d\n", file_count);
    printf("  Base Path: %s\n", argv[2]);
    printf("========================================\n\n");
    printf("Press Ctrl+C to stop...\n");
    
    pthread_create(&g_state.heartbeat_thread, NULL, heartbeat_thread_func, &g_state);
    
    // Simple event loop
    fd_set read_set;
    int max_fd = g_state.client_listen_socket;
    if (g_state.ss_listen_socket > max_fd) max_fd = g_state.ss_listen_socket;
    if (g_state.nm_socket > max_fd) max_fd = g_state.nm_socket;
    
    while (keep_running && g_state.running) {
        FD_ZERO(&read_set);
        FD_SET(g_state.client_listen_socket, &read_set);
        FD_SET(g_state.ss_listen_socket, &read_set);
        FD_SET(g_state.nm_socket, &read_set);
        
        struct timeval timeout = {1, 0};
        int activity = select(max_fd + 1, &read_set, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (keep_running) perror("select");
            break;
        }
        
        if (activity == 0) continue; // Timeout
        
        // Handle incoming connections (basic handling for now)
        if (FD_ISSET(g_state.client_listen_socket, &read_set)) {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            int client_fd = accept(g_state.client_listen_socket, (struct sockaddr*)&addr, &len);
            if (client_fd >= 0) {
                printf("Client connected from %s:%d\n", 
                       inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
                close(client_fd); // TODO: Handle requests
            }
        }
        
        if (FD_ISSET(g_state.ss_listen_socket, &read_set)) {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            int ss_fd = accept(g_state.ss_listen_socket, (struct sockaddr*)&addr, &len);
            if (ss_fd >= 0) {
                printf("SS connected from %s:%d\n", 
                       inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
                close(ss_fd); // TODO: Handle SS requests
            }
        }
        
        if (FD_ISSET(g_state.nm_socket, &read_set)) {
            char buffer[1024];
            int n = recv(g_state.nm_socket, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) {
                printf("Lost connection to Name Server\n");
                g_state.running = false;
                break;
            }
            buffer[n] = '\0';
            printf("NM message: %s\n", buffer);
        }
    }
    
    printf("\nShutting down...\n");
    g_state.running = false;
    pthread_join(g_state.heartbeat_thread, NULL);
    ss_cleanup(&g_state);
    
    printf("Storage Server shutdown complete\n");
    return 0;
}
