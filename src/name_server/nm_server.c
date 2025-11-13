#include "nm_server.h"
#include "../common/protocol.h"
#include "../common/error_codes.h"
#include "../common/logger.h"
#include "../common/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>

// Global state
static NameServerState* g_nm_state = NULL;

// ==================== Initialization ====================

int nm_init(NameServerState* state) {
    memset(state, 0, sizeof(NameServerState));
    
    // Initialize mutexes
    if (pthread_mutex_init(&state->ss_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to initialize SS mutex\n");
        return -1;
    }
    
    if (pthread_mutex_init(&state->client_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to initialize client mutex\n");
        pthread_mutex_destroy(&state->ss_mutex);
        return -1;
    }
    
    if (pthread_mutex_init(&state->file_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to initialize file mutex\n");
        pthread_mutex_destroy(&state->ss_mutex);
        pthread_mutex_destroy(&state->client_mutex);
        return -1;
    }
    
    // Allocate file map (temporary, will be replaced by Trie)
    state->file_map = (FileMetadata*)calloc(MAX_FILES, sizeof(FileMetadata));
    if (!state->file_map) {
        fprintf(stderr, "Failed to allocate file map\n");
        pthread_mutex_destroy(&state->ss_mutex);
        pthread_mutex_destroy(&state->client_mutex);
        pthread_mutex_destroy(&state->file_mutex);
        return -1;
    }
    
    // Create server socket
    state->server_fd = create_server_socket(NM_PORT);
    if (state->server_fd < 0) {
        fprintf(stderr, "Failed to create server socket on port %d\n", NM_PORT);
        free(state->file_map);
        pthread_mutex_destroy(&state->ss_mutex);
        pthread_mutex_destroy(&state->client_mutex);
        pthread_mutex_destroy(&state->file_mutex);
        return -1;
    }
    
    state->running = true;
    g_nm_state = state;
    
    printf("Name Server initialized on port %d\n", NM_PORT);
    log_message("NM", "0.0.0.0", NM_PORT, "system", "INIT", "Name Server started", "SUCCESS");
    
    return 0;
}

void nm_cleanup(NameServerState* state) {
    if (!state) return;
    
    state->running = false;
    
    // Close all client connections
    pthread_mutex_lock(&state->client_mutex);
    for (int i = 0; i < state->client_count; i++) {
        if (state->client_registry[i].is_active) {
            close(state->client_registry[i].client_fd);
        }
    }
    pthread_mutex_unlock(&state->client_mutex);
    
    // Close all SS connections
    pthread_mutex_lock(&state->ss_mutex);
    for (int i = 0; i < state->ss_count; i++) {
        if (state->ss_registry[i].is_active) {
            close(state->ss_registry[i].sockfd);
        }
        if (state->ss_registry[i].file_list) {
            for (int j = 0; j < state->ss_registry[i].file_count; j++) {
                free(state->ss_registry[i].file_list[j]);
            }
            free(state->ss_registry[i].file_list);
        }
    }
    pthread_mutex_unlock(&state->ss_mutex);
    
    // Free file metadata and ACLs
    pthread_mutex_lock(&state->file_mutex);
    for (int i = 0; i < state->file_count; i++) {
        AccessControlEntry* entry = state->file_map[i].acl_head;
        while (entry) {
            AccessControlEntry* next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(state->file_map);
    pthread_mutex_unlock(&state->file_mutex);
    
    // Close server socket
    if (state->server_fd >= 0) {
        close(state->server_fd);
    }
    
    // Destroy mutexes
    pthread_mutex_destroy(&state->ss_mutex);
    pthread_mutex_destroy(&state->client_mutex);
    pthread_mutex_destroy(&state->file_mutex);
    
    printf("Name Server cleanup complete\n");
    log_message("NM", "0.0.0.0", NM_PORT, "system", "SHUTDOWN", "Name Server stopped", "SUCCESS");
}

// ==================== Storage Server Management ====================

int register_storage_server(NameServerState* state, int sockfd) {
    pthread_mutex_lock(&state->ss_mutex);
    
    if (state->ss_count >= MAX_STORAGE_SERVERS) {
        pthread_mutex_unlock(&state->ss_mutex);
        fprintf(stderr, "Maximum storage servers reached\n");
        return -1;
    }
    
    // Receive SS registration info
    Request reg_req;
    if (recv_all(sockfd, &reg_req, sizeof(reg_req)) < 0) {
        pthread_mutex_unlock(&state->ss_mutex);
        fprintf(stderr, "Failed to receive SS registration\n");
        return -1;
    }
    
    // Parse registration data (format: "SS_REGISTER <ss_id> <ip> <nm_port> <client_port> <file_count>")
    int ss_id, nm_port, client_port, file_count;
    char ip[16];
    sscanf(reg_req.data, "SS_REGISTER %d %s %d %d %d", 
           &ss_id, ip, &nm_port, &client_port, &file_count);
    
    // Register SS
    StorageServer* ss = &state->ss_registry[state->ss_count];
    ss->ss_id = ss_id;
    strncpy(ss->ip, ip, sizeof(ss->ip) - 1);
    ss->nm_port = nm_port;
    ss->client_port = client_port;
    ss->is_active = true;
    ss->last_heartbeat = time(NULL);
    ss->sockfd = sockfd;
    ss->file_count = file_count;
    
    // Allocate file list
    if (file_count > 0) {
        ss->file_list = (char**)calloc(file_count, sizeof(char*));
    }
    
    state->ss_count++;
    
    pthread_mutex_unlock(&state->ss_mutex);
    
    printf("Storage Server %d registered: %s:%d (client port: %d)\n", 
           ss_id, ip, nm_port, client_port);
    log_message("NM", ip, nm_port, "SS", "SS_REGISTER", reg_req.data, "SUCCESS");
    
    // Send ACK
    Response resp;
    resp.status_code = SUCCESS;
    snprintf(resp.message, sizeof(resp.message), "SS %d registered successfully", ss_id);
    send_all(sockfd, &resp, sizeof(resp));
    
    return ss_id;
}

StorageServer* find_storage_server(NameServerState* state, int ss_id) {
    pthread_mutex_lock(&state->ss_mutex);
    
    for (int i = 0; i < state->ss_count; i++) {
        if (state->ss_registry[i].ss_id == ss_id && state->ss_registry[i].is_active) {
            pthread_mutex_unlock(&state->ss_mutex);
            return &state->ss_registry[i];
        }
    }
    
    pthread_mutex_unlock(&state->ss_mutex);
    return NULL;
}

StorageServer* find_ss_for_file(NameServerState* state, const char* filename) {
    FileMetadata* file = find_file(state, filename);
    if (!file) return NULL;
    
    return find_storage_server(state, file->ss_id);
}

void update_ss_heartbeat(NameServerState* state, int ss_id) {
    StorageServer* ss = find_storage_server(state, ss_id);
    if (ss) {
        ss->last_heartbeat = time(NULL);
    }
}

// ==================== Client Management ====================

int register_client(NameServerState* state, int client_fd, const char* username) {
    pthread_mutex_lock(&state->client_mutex);
    
    if (state->client_count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&state->client_mutex);
        fprintf(stderr, "Maximum clients reached\n");
        return -1;
    }
    
    ClientInfo* client = &state->client_registry[state->client_count];
    client->client_fd = client_fd;
    strncpy(client->username, username, sizeof(client->username) - 1);
    get_peer_info(client_fd, client->ip, sizeof(client->ip), &client->port);
    client->connected_at = time(NULL);
    client->is_active = true;
    
    state->client_count++;
    
    pthread_mutex_unlock(&state->client_mutex);
    
    printf("Client '%s' connected from %s:%d\n", username, client->ip, client->port);
    log_message("NM", client->ip, client->port, username, "CLIENT_CONNECT", "Client registered", "SUCCESS");
    
    return 0;
}

ClientInfo* find_client(NameServerState* state, int client_fd) {
    pthread_mutex_lock(&state->client_mutex);
    
    for (int i = 0; i < state->client_count; i++) {
        if (state->client_registry[i].client_fd == client_fd && state->client_registry[i].is_active) {
            pthread_mutex_unlock(&state->client_mutex);
            return &state->client_registry[i];
        }
    }
    
    pthread_mutex_unlock(&state->client_mutex);
    return NULL;
}

void remove_client(NameServerState* state, int client_fd) {
    pthread_mutex_lock(&state->client_mutex);
    
    for (int i = 0; i < state->client_count; i++) {
        if (state->client_registry[i].client_fd == client_fd) {
            state->client_registry[i].is_active = false;
            close(client_fd);
            
            printf("Client '%s' disconnected\n", state->client_registry[i].username);
            log_message("NM", state->client_registry[i].ip, state->client_registry[i].port,
                       state->client_registry[i].username, "CLIENT_DISCONNECT", "Client removed", "SUCCESS");
            break;
        }
    }
    
    pthread_mutex_unlock(&state->client_mutex);
}

// ==================== File Management ====================

int add_file_to_registry(NameServerState* state, FileMetadata* file) {
    pthread_mutex_lock(&state->file_mutex);
    
    if (state->file_count >= MAX_FILES) {
        pthread_mutex_unlock(&state->file_mutex);
        fprintf(stderr, "Maximum files reached\n");
        return -1;
    }
    
    // Check if file already exists
    for (int i = 0; i < state->file_count; i++) {
        if (strcmp(state->file_map[i].filename, file->filename) == 0) {
            pthread_mutex_unlock(&state->file_mutex);
            return ERR_FILE_ALREADY_EXISTS;
        }
    }
    
    // Add file
    memcpy(&state->file_map[state->file_count], file, sizeof(FileMetadata));
    state->file_count++;
    
    pthread_mutex_unlock(&state->file_mutex);
    
    printf("File '%s' added to registry (owner: %s, SS: %d)\n", 
           file->filename, file->owner, file->ss_id);
    log_message("NM", "0.0.0.0", NM_PORT, file->owner, "FILE_ADD", file->filename, "SUCCESS");
    
    return SUCCESS;
}

FileMetadata* find_file(NameServerState* state, const char* filename) {
    pthread_mutex_lock(&state->file_mutex);
    
    for (int i = 0; i < state->file_count; i++) {
        if (strcmp(state->file_map[i].filename, filename) == 0) {
            pthread_mutex_unlock(&state->file_mutex);
            return &state->file_map[i];
        }
    }
    
    pthread_mutex_unlock(&state->file_mutex);
    return NULL;
}

int remove_file_from_registry(NameServerState* state, const char* filename) {
    pthread_mutex_lock(&state->file_mutex);
    
    for (int i = 0; i < state->file_count; i++) {
        if (strcmp(state->file_map[i].filename, filename) == 0) {
            // Free ACL
            AccessControlEntry* entry = state->file_map[i].acl_head;
            while (entry) {
                AccessControlEntry* next = entry->next;
                free(entry);
                entry = next;
            }
            
            // Shift remaining files
            for (int j = i; j < state->file_count - 1; j++) {
                memcpy(&state->file_map[j], &state->file_map[j + 1], sizeof(FileMetadata));
            }
            
            state->file_count--;
            pthread_mutex_unlock(&state->file_mutex);
            
            printf("File '%s' removed from registry\n", filename);
            log_message("NM", "0.0.0.0", NM_PORT, "system", "FILE_REMOVE", filename, "SUCCESS");
            
            return SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&state->file_mutex);
    return ERR_FILE_NOT_FOUND;
}

void update_file_metadata(NameServerState* state, const char* filename, FileMetadata* updated) {
    FileMetadata* file = find_file(state, filename);
    if (file) {
        pthread_mutex_lock(&state->file_mutex);
        
        // Update metadata (preserve ACL and owner)
        file->last_modified = updated->last_modified;
        file->last_accessed = updated->last_accessed;
        file->file_size = updated->file_size;
        file->word_count = updated->word_count;
        file->char_count = updated->char_count;
        
        pthread_mutex_unlock(&state->file_mutex);
    }
}

// ==================== Access Control ====================

bool check_read_permission(FileMetadata* file, const char* username) {
    if (!file || !username) return false;
    
    // Owner always has read access
    if (strcmp(file->owner, username) == 0) return true;
    
    // Check ACL
    AccessControlEntry* entry = file->acl_head;
    while (entry) {
        if (strcmp(entry->username, username) == 0) {
            return entry->can_read;
        }
        entry = entry->next;
    }
    
    return false;
}

bool check_write_permission(FileMetadata* file, const char* username) {
    if (!file || !username) return false;
    
    // Owner always has write access
    if (strcmp(file->owner, username) == 0) return true;
    
    // Check ACL
    AccessControlEntry* entry = file->acl_head;
    while (entry) {
        if (strcmp(entry->username, username) == 0) {
            return entry->can_write;
        }
        entry = entry->next;
    }
    
    return false;
}

int add_access(FileMetadata* file, const char* username, bool read, bool write) {
    if (!file || !username) return -1;
    
    // Check if entry already exists
    AccessControlEntry* entry = file->acl_head;
    while (entry) {
        if (strcmp(entry->username, username) == 0) {
            // Update existing entry
            entry->can_read = read;
            entry->can_write = write;
            return SUCCESS;
        }
        entry = entry->next;
    }
    
    // Create new entry
    AccessControlEntry* new_entry = (AccessControlEntry*)malloc(sizeof(AccessControlEntry));
    if (!new_entry) return -1;
    
    strncpy(new_entry->username, username, sizeof(new_entry->username) - 1);
    new_entry->can_read = read;
    new_entry->can_write = write;
    new_entry->next = file->acl_head;
    file->acl_head = new_entry;
    
    return SUCCESS;
}

int remove_access(FileMetadata* file, const char* username) {
    if (!file || !username) return -1;
    
    AccessControlEntry* entry = file->acl_head;
    AccessControlEntry* prev = NULL;
    
    while (entry) {
        if (strcmp(entry->username, username) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                file->acl_head = entry->next;
            }
            free(entry);
            return SUCCESS;
        }
        prev = entry;
        entry = entry->next;
    }
    
    return ERR_FILE_NOT_FOUND;
}

// ==================== Request Routing (Stub implementations) ====================

int route_read_request(NameServerState* state, int client_fd, const char* filename) {
    // Find file and SS
    FileMetadata* file = find_file(state, filename);
    if (!file) {
        Response resp;
        resp.status_code = ERR_FILE_NOT_FOUND;
        strncpy(resp.message, get_error_message(ERR_FILE_NOT_FOUND), sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check permission
    ClientInfo* client = find_client(state, client_fd);
    if (!check_read_permission(file, client->username)) {
        Response resp;
        resp.status_code = ERR_UNAUTHORIZED_ACCESS;
        strncpy(resp.message, get_error_message(ERR_UNAUTHORIZED_ACCESS), sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_UNAUTHORIZED_ACCESS;
    }
    
    // Find SS
    StorageServer* ss = find_storage_server(state, file->ss_id);
    if (!ss || !ss->is_active) {
        Response resp;
        resp.status_code = ERR_SS_UNAVAILABLE;
        strncpy(resp.message, get_error_message(ERR_SS_UNAVAILABLE), sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_SS_UNAVAILABLE;
    }
    
    // Send SS info to client
    Response resp;
    resp.status_code = SUCCESS;
    strncpy(resp.ss_ip, ss->ip, sizeof(resp.ss_ip) - 1);
    resp.ss_port = ss->client_port;
    snprintf(resp.message, sizeof(resp.message), "Connect to SS at %s:%d", ss->ip, ss->client_port);
    send_all(client_fd, &resp, sizeof(resp));
    
    log_message("NM", client->ip, client->port, client->username, 
                "READ", filename, "ROUTED_TO_SS");
    
    return SUCCESS;
}

int route_write_request(NameServerState* state, int client_fd, const char* filename, int sentence_idx) {
    // Similar to read, but check write permission
    FileMetadata* file = find_file(state, filename);
    if (!file) {
        Response resp;
        resp.status_code = ERR_FILE_NOT_FOUND;
        strncpy(resp.message, get_error_message(ERR_FILE_NOT_FOUND), sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_FILE_NOT_FOUND;
    }
    
    ClientInfo* client = find_client(state, client_fd);
    if (!check_write_permission(file, client->username)) {
        Response resp;
        resp.status_code = ERR_UNAUTHORIZED_ACCESS;
        strncpy(resp.message, get_error_message(ERR_UNAUTHORIZED_ACCESS), sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_UNAUTHORIZED_ACCESS;
    }
    
    StorageServer* ss = find_storage_server(state, file->ss_id);
    if (!ss || !ss->is_active) {
        Response resp;
        resp.status_code = ERR_SS_UNAVAILABLE;
        strncpy(resp.message, get_error_message(ERR_SS_UNAVAILABLE), sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_SS_UNAVAILABLE;
    }
    
    Response resp;
    resp.status_code = SUCCESS;
    strncpy(resp.ss_ip, ss->ip, sizeof(resp.ss_ip) - 1);
    resp.ss_port = ss->client_port;
    snprintf(resp.message, sizeof(resp.message), "Connect to SS at %s:%d", ss->ip, ss->client_port);
    send_all(client_fd, &resp, sizeof(resp));
    
    log_message("NM", client->ip, client->port, client->username, 
                "WRITE", filename, "ROUTED_TO_SS");
    
    return SUCCESS;
}

int route_create_request(NameServerState* state, int client_fd, const char* filename, const char* owner) {
    // Check if file exists
    if (find_file(state, filename) != NULL) {
        Response resp;
        resp.status_code = ERR_FILE_ALREADY_EXISTS;
        strncpy(resp.message, get_error_message(ERR_FILE_ALREADY_EXISTS), sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_FILE_ALREADY_EXISTS;
    }
    
    // Select SS (round-robin for now)
    pthread_mutex_lock(&state->ss_mutex);
    if (state->ss_count == 0) {
        pthread_mutex_unlock(&state->ss_mutex);
        Response resp;
        resp.status_code = ERR_SS_UNAVAILABLE;
        strncpy(resp.message, "No storage servers available", sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_SS_UNAVAILABLE;
    }
    
    // Simple round-robin: use file_count % ss_count
    int ss_idx = state->file_count % state->ss_count;
    StorageServer* ss = &state->ss_registry[ss_idx];
    pthread_mutex_unlock(&state->ss_mutex);
    
    // Add to registry
    FileMetadata file;
    memset(&file, 0, sizeof(file));
    strncpy(file.filename, filename, sizeof(file.filename) - 1);
    strncpy(file.owner, owner, sizeof(file.owner) - 1);
    file.ss_id = ss->ss_id;
    file.created_at = time(NULL);
    file.last_modified = file.created_at;
    file.last_accessed = file.created_at;
    file.acl_head = NULL;
    
    add_file_to_registry(state, &file);
    
    // Forward to SS (stub - will implement later)
    Response resp;
    resp.status_code = SUCCESS;
    snprintf(resp.message, sizeof(resp.message), "File '%s' created on SS %d", filename, ss->ss_id);
    send_all(client_fd, &resp, sizeof(resp));
    
    ClientInfo* client = find_client(state, client_fd);
    log_message("NM", client->ip, client->port, owner, "CREATE", filename, "SUCCESS");
    
    return SUCCESS;
}

int route_delete_request(NameServerState* state, int client_fd, const char* filename) {
    FileMetadata* file = find_file(state, filename);
    if (!file) {
        Response resp;
        resp.status_code = ERR_FILE_NOT_FOUND;
        strncpy(resp.message, get_error_message(ERR_FILE_NOT_FOUND), sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_FILE_NOT_FOUND;
    }
    
    ClientInfo* client = find_client(state, client_fd);
    // Only owner can delete
    if (strcmp(file->owner, client->username) != 0) {
        Response resp;
        resp.status_code = ERR_PERMISSION_DENIED;
        strncpy(resp.message, "Only owner can delete file", sizeof(resp.message) - 1);
        send_all(client_fd, &resp, sizeof(resp));
        return ERR_PERMISSION_DENIED;
    }
    
    // Remove from registry
    remove_file_from_registry(state, filename);
    
    // Forward to SS (stub - will implement later)
    Response resp;
    resp.status_code = SUCCESS;
    snprintf(resp.message, sizeof(resp.message), "File '%s' deleted", filename);
    send_all(client_fd, &resp, sizeof(resp));
    
    log_message("NM", client->ip, client->port, client->username, "DELETE", filename, "SUCCESS");
    
    return SUCCESS;
}
