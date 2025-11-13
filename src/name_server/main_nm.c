#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include "nm_server.h"
#include "../common/protocol.h"
#include "../common/error_codes.h"
#include "../common/logger.h"
#include "../common/utils.h"

// Global state for signal handling
static NameServerState g_state;
static volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
    g_state.running = false;
    printf("\nShutdown signal received...\n");
}

int handle_client_request(NameServerState* state, int client_fd) {
    Request req;
    
    // Receive request
    int bytes = recv_message(client_fd, &req, sizeof(req));
    if (bytes <= 0) {
        // Client disconnected
        remove_client(state, client_fd);
        return -1;
    }
    
    ClientInfo* client = find_client(state, client_fd);
    if (!client) {
        fprintf(stderr, "Unknown client fd: %d\n", client_fd);
        return -1;
    }
    
    printf("Request from '%s': cmd=%d, filename='%s'\n", 
           client->username, req.cmd, req.filename);
    
    // Route based on command
    switch (req.cmd) {
        case CMD_READ:
            return route_read_request(state, client_fd, req.filename);
            
        case CMD_WRITE:
            return route_write_request(state, client_fd, req.filename, req.sentence_index);
            
        case CMD_CREATE:
            return route_create_request(state, client_fd, req.filename, client->username);
            
        case CMD_DELETE:
            return route_delete_request(state, client_fd, req.filename);
            
        case CMD_VIEW:
        case CMD_INFO:
        case CMD_LIST:
        case CMD_ADDACCESS:
        case CMD_REMACCESS:
        case CMD_UNDO:
        case CMD_STREAM:
        case CMD_EXEC:
            // Will implement these in next phase
            {
                Response resp;
                resp.status_code = ERR_INVALID_COMMAND;
                snprintf(resp.message, sizeof(resp.message), 
                        "Command %d not yet implemented", req.cmd);
                send_all(client_fd, &resp, sizeof(resp));
            }
            break;
            
        default:
            {
                Response resp;
                resp.status_code = ERR_INVALID_COMMAND;
                strncpy(resp.message, get_error_message(ERR_INVALID_COMMAND), 
                       sizeof(resp.message) - 1);
                send_all(client_fd, &resp, sizeof(resp));
            }
            break;
    }
    
    return 0;
}

void run_server_loop(NameServerState* state) {
    fd_set read_fds, master_fds;
    int max_fd = state->server_fd;
    
    FD_ZERO(&master_fds);
    FD_SET(state->server_fd, &master_fds);
    
    printf("Name Server listening for connections...\n");
    
    while (keep_running && state->running) {
        read_fds = master_fds;
        
        // Set timeout for select
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (!keep_running) break;
            perror("select error");
            continue;
        }
        
        if (activity == 0) {
            // Timeout - continue
            continue;
        }
        
        // Check for new connections
        if (FD_ISSET(state->server_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            
            int new_fd = accept(state->server_fd, (struct sockaddr*)&client_addr, &addr_len);
            if (new_fd < 0) {
                perror("accept failed");
                continue;
            }
            
            // Receive initial identification message
            Request ident_req;
            if (recv_all(new_fd, &ident_req, sizeof(ident_req)) < 0) {
                fprintf(stderr, "Failed to receive identification\n");
                close(new_fd);
                continue;
            }
            
            // Check if it's SS or Client
            if (starts_with(ident_req.data, "SS_REGISTER")) {
                // Storage Server registration
                int ss_id = register_storage_server(state, new_fd);
                if (ss_id >= 0) {
                    FD_SET(new_fd, &master_fds);
                    if (new_fd > max_fd) max_fd = new_fd;
                } else {
                    close(new_fd);
                }
            } else if (starts_with(ident_req.data, "CLIENT_REGISTER")) {
                // Client registration (format: "CLIENT_REGISTER <username>")
                char username[64];
                sscanf(ident_req.data, "CLIENT_REGISTER %s", username);
                
                if (register_client(state, new_fd, username) == 0) {
                    FD_SET(new_fd, &master_fds);
                    if (new_fd > max_fd) max_fd = new_fd;
                    
                    // Send ACK
                    Response resp;
                    resp.status_code = SUCCESS;
                    snprintf(resp.message, sizeof(resp.message), 
                            "Welcome %s!", username);
                    send_all(new_fd, &resp, sizeof(resp));
                } else {
                    close(new_fd);
                }
            } else {
                fprintf(stderr, "Unknown connection type\n");
                close(new_fd);
            }
        }
        
        // Check existing connections for data
        for (int fd = 0; fd <= max_fd; fd++) {
            if (fd == state->server_fd) continue;
            
            if (FD_ISSET(fd, &read_fds)) {
                // Check if it's a client
                ClientInfo* client = find_client(state, fd);
                if (client) {
                    if (handle_client_request(state, fd) < 0) {
                        // Client disconnected
                        FD_CLR(fd, &master_fds);
                    }
                    continue;
                }
                
                // Check if it's a storage server
                bool is_ss = false;
                pthread_mutex_lock(&state->ss_mutex);
                for (int i = 0; i < state->ss_count; i++) {
                    if (state->ss_registry[i].sockfd == fd && 
                        state->ss_registry[i].is_active) {
                        is_ss = true;
                        // Handle SS message (heartbeat, file updates, etc.)
                        // For now, just update heartbeat
                        state->ss_registry[i].last_heartbeat = time(NULL);
                        
                        // Read and discard message (stub)
                        char buffer[4096];
                        recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
                        break;
                    }
                }
                pthread_mutex_unlock(&state->ss_mutex);
                
                if (!is_ss) {
                    // Unknown connection - close it
                    FD_CLR(fd, &master_fds);
                    close(fd);
                }
            }
        }
    }
    
    printf("Server loop terminated\n");
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    printf("╔════════════════════════════════════════╗\n");
    printf("║     Docs++ Name Server v1.0            ║\n");
    printf("╚════════════════════════════════════════╝\n\n");
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize logger
    init_logger("logs/name_server.log");
    
    // Initialize Name Server
    if (nm_init(&g_state) < 0) {
        fprintf(stderr, "Failed to initialize Name Server\n");
        return 1;
    }
    
    printf("\n");
    printf("========================================\n");
    printf("Name Server Status:\n");
    printf("  Port: %d\n", NM_PORT);
    printf("  Max Storage Servers: %d\n", MAX_STORAGE_SERVERS);
    printf("  Max Clients: %d\n", MAX_CLIENTS);
    printf("  Max Files: %d\n", MAX_FILES);
    printf("========================================\n");
    printf("\n");
    
    // Run server loop
    run_server_loop(&g_state);
    
    // Cleanup
    nm_cleanup(&g_state);
    close_logger();
    
    printf("\nName Server shut down gracefully\n");
    
    return 0;
}

