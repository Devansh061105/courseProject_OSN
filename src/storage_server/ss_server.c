#include "ss_server.h"
#include "../common/utils.h"
#include "../common/logger.h"
#include "../common/error_codes.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ===============================================
 * INITIALIZATION FUNCTIONS
 * =============================================== */

int ss_init(StorageServerState* state, int ss_id, const char* base_path,
            const char* nm_ip, int nm_port, int client_port, int ss_port) {
    
    if (!state || !base_path || !nm_ip) {
        return -1;
    }
    
    memset(state, 0, sizeof(StorageServerState));
    
    state->ss_id = ss_id;
    strncpy(state->base_path, base_path, sizeof(state->base_path) - 1);
    strncpy(state->nm_ip, nm_ip, sizeof(state->nm_ip) - 1);
    state->nm_port = nm_port;
    state->client_port = client_port;
    state->ss_port = ss_port;
    state->running = true;
    state->file_count = 0;
    state->active_locks = NULL;
    
    // Initialize mutexes
    pthread_mutex_init(&state->registry_mutex, NULL);
    pthread_mutex_init(&state->lock_list_mutex, NULL);
    
    // Create base directory if it doesn't exist
    struct stat st;
    if (stat(base_path, &st) == -1) {
        if (mkdir(base_path, 0755) == -1) {
            perror("mkdir base_path");
            return -1;
        }
    }
    
    // Create client listening socket
    state->client_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (state->client_listen_socket < 0) {
        perror("socket client");
        return -1;
    }
    
    int opt = 1;
    setsockopt(state->client_listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(client_port);
    
    if (bind(state->client_listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind client");
        close(state->client_listen_socket);
        return -1;
    }
    
    if (listen(state->client_listen_socket, 10) < 0) {
        perror("listen client");
        close(state->client_listen_socket);
        return -1;
    }
    
    // Create SS listening socket
    state->ss_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (state->ss_listen_socket < 0) {
        perror("socket ss");
        close(state->client_listen_socket);
        return -1;
    }
    
    setsockopt(state->ss_listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    addr.sin_port = htons(ss_port);
    if (bind(state->ss_listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind ss");
        close(state->client_listen_socket);
        close(state->ss_listen_socket);
        return -1;
    }
    
    if (listen(state->ss_listen_socket, 5) < 0) {
        perror("listen ss");
        close(state->client_listen_socket);
        close(state->ss_listen_socket);
        return -1;
    }
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), 
             "INIT - SS_ID=%d ClientPort=%d SSPort=%d", 
             ss_id, client_port, ss_port);
    log_message("SS", "0.0.0.0", client_port, "system", "INIT", log_msg, "SUCCESS");
    
    return 0;
}

int register_with_name_server(StorageServerState* state) {
    if (!state) return -1;
    
    // Connect to name server
    state->nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (state->nm_socket < 0) {
        perror("socket nm");
        return -1;
    }
    
    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(state->nm_port);
    inet_pton(AF_INET, state->nm_ip, &nm_addr.sin_addr);
    
    if (connect(state->nm_socket, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("connect nm");
        close(state->nm_socket);
        return -1;
    }
    
    // Send registration message
    char reg_msg[1024];
    int offset = 0;
    
    // Header: SS_REGISTER
    strcpy(reg_msg, "SS_REGISTER\n");
    offset += strlen("SS_REGISTER\n");
    
    // SS_ID
    offset += snprintf(reg_msg + offset, sizeof(reg_msg) - offset, 
                       "SS_ID:%d\n", state->ss_id);
    
    // Ports
    offset += snprintf(reg_msg + offset, sizeof(reg_msg) - offset,
                       "CLIENT_PORT:%d\n", state->client_port);
    offset += snprintf(reg_msg + offset, sizeof(reg_msg) - offset,
                       "SS_PORT:%d\n", state->ss_port);
    
    // Send file list
    pthread_mutex_lock(&state->registry_mutex);
    offset += snprintf(reg_msg + offset, sizeof(reg_msg) - offset,
                       "FILE_COUNT:%d\n", state->file_count);
    
    for (int i = 0; i < state->file_count && i < 50; i++) {
        offset += snprintf(reg_msg + offset, sizeof(reg_msg) - offset,
                           "FILE:%s\n", state->files[i].filepath);
    }
    pthread_mutex_unlock(&state->registry_mutex);
    
    // Send message
    if (send_all(state->nm_socket, reg_msg, strlen(reg_msg)) < 0) {
        perror("send registration");
        close(state->nm_socket);
        return -1;
    }
    
    // Wait for acknowledgment
    char ack[64];
    int n = recv(state->nm_socket, ack, sizeof(ack) - 1, 0);
    if (n > 0) {
        ack[n] = '\0';
        if (strstr(ack, "SUCCESS")) {
            log_message("SS", state->nm_ip, state->nm_port, "system",
                       "REGISTER", "NM_REGISTRATION", "SUCCESS");
            return 0;
        }
    }
    
    close(state->nm_socket);
    return -1;
}

void* heartbeat_thread_func(void* arg) {
    StorageServerState* state = (StorageServerState*)arg;
    
    while (state->running) {
        sleep(30); // Send heartbeat every 30 seconds
        
        if (state->nm_socket > 0) {
            char heartbeat[] = "HEARTBEAT\n";
            if (send_all(state->nm_socket, heartbeat, strlen(heartbeat)) < 0) {
                log_message("SS", state->nm_ip, state->nm_port, "system",
                           "HEARTBEAT", "Failed", "ERROR");
            }
        }
    }
    
    return NULL;
}

void ss_cleanup(StorageServerState* state) {
    if (!state) return;
    
    state->running = false;
    
    // Close sockets
    if (state->nm_socket > 0) close(state->nm_socket);
    if (state->client_listen_socket > 0) close(state->client_listen_socket);
    if (state->ss_listen_socket > 0) close(state->ss_listen_socket);
    
    // Clean up locks
    pthread_mutex_lock(&state->lock_list_mutex);
    SentenceLock* lock = state->active_locks;
    while (lock) {
        SentenceLock* next = lock->next;
        pthread_mutex_destroy(&lock->lock_mutex);
        free(lock);
        lock = next;
    }
    pthread_mutex_unlock(&state->lock_list_mutex);
    
    // Destroy mutexes
    pthread_mutex_destroy(&state->registry_mutex);
    pthread_mutex_destroy(&state->lock_list_mutex);
    
    for (int i = 0; i < state->file_count; i++) {
        pthread_mutex_destroy(&state->files[i].file_mutex);
    }
    
    log_message("SS", "0.0.0.0", state->client_port, "system",
               "SHUTDOWN", "Complete", "SUCCESS");
}

/* ===============================================
 * FILE OPERATIONS
 * =============================================== */

int scan_and_register_files(StorageServerState* state) {
    if (!state) return -1;
    
    DIR* dir = opendir(state->base_path);
    if (!dir) {
        perror("opendir");
        return -1;
    }
    
    struct dirent* entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", 
                 state->base_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            bool is_dir = S_ISDIR(st.st_mode);
            if (add_file_to_registry(state, entry->d_name, is_dir)) {
                count++;
            }
        }
    }
    
    closedir(dir);
    
    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "SCAN_FILES - Found %d files", count);
    log_message("SS", "0.0.0.0", state->client_port, "system", "SCAN", log_msg, "SUCCESS");
    
    return count;
}

FileEntry* add_file_to_registry(StorageServerState* state, 
                                 const char* filepath, bool is_directory) {
    if (!state || !filepath) return NULL;
    
    pthread_mutex_lock(&state->registry_mutex);
    
    if (state->file_count >= MAX_FILES) {
        pthread_mutex_unlock(&state->registry_mutex);
        return NULL;
    }
    
    FileEntry* entry = &state->files[state->file_count];
    memset(entry, 0, sizeof(FileEntry));
    
    strncpy(entry->filepath, filepath, sizeof(entry->filepath) - 1);
    snprintf(entry->full_path, sizeof(entry->full_path), "%s/%s",
             state->base_path, filepath);
    entry->is_directory = is_directory;
    entry->locks = NULL;
    
    struct stat st;
    if (stat(entry->full_path, &st) == 0) {
        entry->file_size = st.st_size;
        entry->created_at = st.st_ctime;
        entry->modified_at = st.st_mtime;
    }
    
    if (!is_directory) {
        entry->sentence_count = count_sentences(entry->full_path);
    }
    
    pthread_mutex_init(&entry->file_mutex, NULL);
    
    state->file_count++;
    pthread_mutex_unlock(&state->registry_mutex);
    
    return entry;
}

FileEntry* find_file(StorageServerState* state, const char* filepath) {
    if (!state || !filepath) return NULL;
    
    pthread_mutex_lock(&state->registry_mutex);
    
    for (int i = 0; i < state->file_count; i++) {
        if (strcmp(state->files[i].filepath, filepath) == 0) {
            pthread_mutex_unlock(&state->registry_mutex);
            return &state->files[i];
        }
    }
    
    pthread_mutex_unlock(&state->registry_mutex);
    return NULL;
}

int create_file(StorageServerState* state, const char* filepath) {
    if (!state || !filepath) return ERR_INVALID_OPERATION;
    
    char full_path[MAX_PATH_LEN];
    snprintf(full_path, sizeof(full_path), "%s/%s", state->base_path, filepath);
    
    // Check if file already exists
    if (access(full_path, F_OK) == 0) {
        return ERR_FILE_EXISTS;
    }
    
    // Create file
    FILE* fp = fopen(full_path, "w");
    if (!fp) {
        return ERR_INVALID_OPERATION;
    }
    fclose(fp);
    
    // Add to registry
    if (!add_file_to_registry(state, filepath, false)) {
        unlink(full_path);
        return ERR_INVALID_OPERATION;
    }
    
    log_message("SS", "0.0.0.0", state->client_port, "system",
               "CREATE", filepath, "SUCCESS");
    
    return ERR_SUCCESS;
}

int delete_file(StorageServerState* state, const char* filepath) {
    if (!state || !filepath) return ERR_INVALID_OPERATION;
    
    FileEntry* entry = find_file(state, filepath);
    if (!entry) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check if file has active locks
    if (entry->locks != NULL) {
        return ERR_FILE_LOCKED;
    }
    
    // Delete physical file
    if (unlink(entry->full_path) < 0) {
        return ERR_INVALID_OPERATION;
    }
    
    // Remove from registry
    pthread_mutex_lock(&state->registry_mutex);
    pthread_mutex_destroy(&entry->file_mutex);
    
    // Shift remaining entries
    for (int i = (entry - state->files); i < state->file_count - 1; i++) {
        state->files[i] = state->files[i + 1];
    }
    state->file_count--;
    
    pthread_mutex_unlock(&state->registry_mutex);
    
    log_message("SS", "0.0.0.0", state->client_port, "system",
               "DELETE", filepath, "SUCCESS");
    
    return ERR_SUCCESS;
}

int copy_file_to_ss(StorageServerState* state, const char* filepath,
                    const char* dest_ss_ip, int dest_ss_port) {
    if (!state || !filepath || !dest_ss_ip) return ERR_INVALID_OPERATION;
    
    FileEntry* entry = find_file(state, filepath);
    if (!entry) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Connect to destination SS
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return ERR_CONNECTION_FAILED;
    }
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_ss_port);
    inet_pton(AF_INET, dest_ss_ip, &dest_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
        close(sock);
        return ERR_CONNECTION_FAILED;
    }
    
    // Send COPY command
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "COPY %s\n", filepath);
    send_all(sock, cmd, strlen(cmd));
    
    // Send file content
    FILE* fp = fopen(entry->full_path, "r");
    if (!fp) {
        close(sock);
        return ERR_INVALID_OPERATION;
    }
    
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send_all(sock, buffer, n);
    }
    
    fclose(fp);
    close(sock);
    
    log_message("SS", dest_ss_ip, dest_ss_port, "system",
               "COPY", filepath, "SUCCESS");
    
    return ERR_SUCCESS;
}

/* ===============================================
 * SENTENCE OPERATIONS
 * =============================================== */

int count_sentences(const char* filepath) {
    if (!filepath) return -1;
    
    FILE* fp = fopen(filepath, "r");
    if (!fp) return -1;
    
    int count = 0;
    int ch;
    bool in_sentence = false;
    
    while ((ch = fgetc(fp)) != EOF) {
        if (!in_sentence && ch != ' ' && ch != '\t' && ch != '\n') {
            in_sentence = true;
        }
        
        if (in_sentence && (ch == '.' || ch == '!' || ch == '?')) {
            count++;
            in_sentence = false;
        }
    }
    
    // If file ends without delimiter and has content, count as one sentence
    if (in_sentence) {
        count++;
    }
    
    fclose(fp);
    return count > 0 ? count : 0;
}

int read_sentence(const char* filepath, int sentence_idx, 
                  char* buffer, size_t buffer_size) {
    if (!filepath || !buffer || buffer_size == 0) return -1;
    
    FILE* fp = fopen(filepath, "r");
    if (!fp) return -1;
    
    int current_sentence = 0;
    size_t pos = 0;
    int ch;
    bool in_sentence = false;
    
    while ((ch = fgetc(fp)) != EOF && pos < buffer_size - 1) {
        if (!in_sentence && ch != ' ' && ch != '\t' && ch != '\n') {
            in_sentence = true;
        }
        
        if (current_sentence == sentence_idx && in_sentence) {
            buffer[pos++] = ch;
            
            if (ch == '.' || ch == '!' || ch == '?') {
                break;
            }
        }
        
        if (in_sentence && (ch == '.' || ch == '!' || ch == '?')) {
            if (current_sentence == sentence_idx) {
                break;
            }
            current_sentence++;
            in_sentence = false;
        }
    }
    
    buffer[pos] = '\0';
    fclose(fp);
    
    return pos;
}

int write_sentence(const char* filepath, int sentence_idx, const char* content) {
    if (!filepath || !content) return ERR_INVALID_OPERATION;
    
    // Read entire file
    FILE* fp = fopen(filepath, "r");
    if (!fp) return ERR_FILE_NOT_FOUND;
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* file_content = malloc(file_size + 1);
    if (!file_content) {
        fclose(fp);
        return ERR_INVALID_OPERATION;
    }
    
    fread(file_content, 1, file_size, fp);
    file_content[file_size] = '\0';
    fclose(fp);
    
    // Find sentence boundaries
    int current_sentence = 0;
    size_t start_pos = 0, end_pos = 0;
    bool in_sentence = false;
    
    for (size_t i = 0; i < (size_t)file_size; i++) {
        char ch = file_content[i];
        
        if (!in_sentence && ch != ' ' && ch != '\t' && ch != '\n') {
            if (current_sentence == sentence_idx) {
                start_pos = i;
            }
            in_sentence = true;
        }
        
        if (in_sentence && (ch == '.' || ch == '!' || ch == '?')) {
            if (current_sentence == sentence_idx) {
                end_pos = i + 1;
                break;
            }
            current_sentence++;
            in_sentence = false;
        }
    }
    
    if (current_sentence != sentence_idx) {
        free(file_content);
        return ERR_INVALID_OPERATION;
    }
    
    // Build new content
    size_t new_size = start_pos + strlen(content) + (file_size - end_pos);
    char* new_content = malloc(new_size + 1);
    if (!new_content) {
        free(file_content);
        return ERR_INVALID_OPERATION;
    }
    
    memcpy(new_content, file_content, start_pos);
    strcpy(new_content + start_pos, content);
    memcpy(new_content + start_pos + strlen(content),
           file_content + end_pos, file_size - end_pos);
    new_content[new_size] = '\0';
    
    // Write back to file
    fp = fopen(filepath, "w");
    if (!fp) {
        free(file_content);
        free(new_content);
        return ERR_INVALID_OPERATION;
    }
    
    fwrite(new_content, 1, new_size, fp);
    fclose(fp);
    
    free(file_content);
    free(new_content);
    
    return ERR_SUCCESS;
}

int append_to_file(const char* filepath, const char* content) {
    if (!filepath || !content) return ERR_INVALID_OPERATION;
    
    FILE* fp = fopen(filepath, "a");
    if (!fp) return ERR_FILE_NOT_FOUND;
    
    fprintf(fp, "%s", content);
    fclose(fp);
    
    return ERR_SUCCESS;
}

/* ===============================================
 * LOCKING MECHANISMS
 * =============================================== */

int acquire_read_lock(StorageServerState* state, const char* filepath,
                      int sentence_idx, int client_fd) {
    if (!state || !filepath) return ERR_INVALID_OPERATION;
    
    pthread_mutex_lock(&state->lock_list_mutex);
    
    // Check for existing write lock
    SentenceLock* lock = state->active_locks;
    while (lock) {
        if (strcmp(lock->filepath, filepath) == 0 && 
            lock->sentence_idx == sentence_idx && 
            lock->is_write_lock) {
            pthread_mutex_unlock(&state->lock_list_mutex);
            return ERR_FILE_LOCKED;
        }
        lock = lock->next;
    }
    
    // Check for existing read lock by this client
    lock = state->active_locks;
    while (lock) {
        if (strcmp(lock->filepath, filepath) == 0 && 
            lock->sentence_idx == sentence_idx && 
            !lock->is_write_lock) {
            lock->read_count++;
            pthread_mutex_unlock(&state->lock_list_mutex);
            return ERR_SUCCESS;
        }
        lock = lock->next;
    }
    
    // Create new read lock
    SentenceLock* new_lock = malloc(sizeof(SentenceLock));
    if (!new_lock) {
        pthread_mutex_unlock(&state->lock_list_mutex);
        return ERR_INVALID_OPERATION;
    }
    
    strncpy(new_lock->filepath, filepath, sizeof(new_lock->filepath) - 1);
    new_lock->sentence_idx = sentence_idx;
    new_lock->client_fd = client_fd;
    new_lock->is_write_lock = false;
    new_lock->read_count = 1;
    new_lock->acquired_at = time(NULL);
    pthread_mutex_init(&new_lock->lock_mutex, NULL);
    
    new_lock->next = state->active_locks;
    state->active_locks = new_lock;
    
    pthread_mutex_unlock(&state->lock_list_mutex);
    
    return ERR_SUCCESS;
}

int acquire_write_lock(StorageServerState* state, const char* filepath,
                       int sentence_idx, int client_fd) {
    if (!state || !filepath) return ERR_INVALID_OPERATION;
    
    pthread_mutex_lock(&state->lock_list_mutex);
    
    // Check for ANY existing lock on this sentence
    SentenceLock* lock = state->active_locks;
    while (lock) {
        if (strcmp(lock->filepath, filepath) == 0 && 
            lock->sentence_idx == sentence_idx) {
            pthread_mutex_unlock(&state->lock_list_mutex);
            return ERR_FILE_LOCKED;
        }
        lock = lock->next;
    }
    
    // Create new write lock
    SentenceLock* new_lock = malloc(sizeof(SentenceLock));
    if (!new_lock) {
        pthread_mutex_unlock(&state->lock_list_mutex);
        return ERR_INVALID_OPERATION;
    }
    
    strncpy(new_lock->filepath, filepath, sizeof(new_lock->filepath) - 1);
    new_lock->sentence_idx = sentence_idx;
    new_lock->client_fd = client_fd;
    new_lock->is_write_lock = true;
    new_lock->read_count = 0;
    new_lock->acquired_at = time(NULL);
    pthread_mutex_init(&new_lock->lock_mutex, NULL);
    
    new_lock->next = state->active_locks;
    state->active_locks = new_lock;
    
    pthread_mutex_unlock(&state->lock_list_mutex);
    
    return ERR_SUCCESS;
}

int release_lock(StorageServerState* state, const char* filepath,
                 int sentence_idx, int client_fd) {
    if (!state || !filepath) return ERR_INVALID_OPERATION;
    
    pthread_mutex_lock(&state->lock_list_mutex);
    
    SentenceLock* lock = state->active_locks;
    SentenceLock* prev = NULL;
    
    while (lock) {
        if (strcmp(lock->filepath, filepath) == 0 && 
            lock->sentence_idx == sentence_idx &&
            lock->client_fd == client_fd) {
            
            if (!lock->is_write_lock) {
                lock->read_count--;
                if (lock->read_count > 0) {
                    pthread_mutex_unlock(&state->lock_list_mutex);
                    return ERR_SUCCESS;
                }
            }
            
            // Remove lock
            if (prev) {
                prev->next = lock->next;
            } else {
                state->active_locks = lock->next;
            }
            
            pthread_mutex_destroy(&lock->lock_mutex);
            free(lock);
            
            pthread_mutex_unlock(&state->lock_list_mutex);
            return ERR_SUCCESS;
        }
        
        prev = lock;
        lock = lock->next;
    }
    
    pthread_mutex_unlock(&state->lock_list_mutex);
    return ERR_INVALID_OPERATION;
}

int release_all_locks_for_client(StorageServerState* state, int client_fd) {
    if (!state) return 0;
    
    pthread_mutex_lock(&state->lock_list_mutex);
    
    int count = 0;
    SentenceLock* lock = state->active_locks;
    SentenceLock* prev = NULL;
    
    while (lock) {
        SentenceLock* next = lock->next;
        
        if (lock->client_fd == client_fd) {
            if (prev) {
                prev->next = next;
            } else {
                state->active_locks = next;
            }
            
            pthread_mutex_destroy(&lock->lock_mutex);
            free(lock);
            count++;
        } else {
            prev = lock;
        }
        
        lock = next;
    }
    
    pthread_mutex_unlock(&state->lock_list_mutex);
    
    return count;
}

bool is_sentence_locked(StorageServerState* state, const char* filepath,
                        int sentence_idx) {
    if (!state || !filepath) return false;
    
    pthread_mutex_lock(&state->lock_list_mutex);
    
    SentenceLock* lock = state->active_locks;
    while (lock) {
        if (strcmp(lock->filepath, filepath) == 0 && 
            lock->sentence_idx == sentence_idx) {
            pthread_mutex_unlock(&state->lock_list_mutex);
            return true;
        }
        lock = lock->next;
    }
    
    pthread_mutex_unlock(&state->lock_list_mutex);
    return false;
}

/* ===============================================
 * REQUEST HANDLERS
 * =============================================== */

int handle_read_request(StorageServerState* state, int client_fd, 
                        const char* filepath) {
    if (!state || !filepath) return ERR_INVALID_OPERATION;
    
    FileEntry* entry = find_file(state, filepath);
    if (!entry) {
        send_all(client_fd, "ERROR:FILE_NOT_FOUND\n", 21);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Read entire file
    FILE* fp = fopen(entry->full_path, "r");
    if (!fp) {
        send_all(client_fd, "ERROR:CANNOT_READ\n", 18);
        return ERR_INVALID_OPERATION;
    }
    
    // Send success header
    char header[128];
    snprintf(header, sizeof(header), "SUCCESS\nSIZE:%ld\n", entry->file_size);
    send_all(client_fd, header, strlen(header));
    
    // Send file content
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send_all(client_fd, buffer, n);
    }
    
    fclose(fp);
    
    log_message("SS", "client", client_fd, "user", "READ", filepath, "SUCCESS");
    
    return ERR_SUCCESS;
}

int handle_write_request(StorageServerState* state, int client_fd,
                         const char* filepath, int sentence_idx, 
                         const char* content) {
    if (!state || !filepath || !content) return ERR_INVALID_OPERATION;
    
    FileEntry* entry = find_file(state, filepath);
    if (!entry) {
        send_all(client_fd, "ERROR:FILE_NOT_FOUND\n", 21);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Acquire write lock
    int result = acquire_write_lock(state, filepath, sentence_idx, client_fd);
    if (result != ERR_SUCCESS) {
        send_all(client_fd, "ERROR:FILE_LOCKED\n", 18);
        return result;
    }
    
    // Write sentence
    result = write_sentence(entry->full_path, sentence_idx, content);
    
    // Release lock
    release_lock(state, filepath, sentence_idx, client_fd);
    
    if (result == ERR_SUCCESS) {
        // Update metadata
        struct stat st;
        if (stat(entry->full_path, &st) == 0) {
            entry->file_size = st.st_size;
            entry->modified_at = st.st_mtime;
            entry->sentence_count = count_sentences(entry->full_path);
        }
        
        send_all(client_fd, "SUCCESS\n", 8);
        log_message("SS", "client", client_fd, "user", "WRITE", filepath, "SUCCESS");
    } else {
        send_all(client_fd, "ERROR:WRITE_FAILED\n", 19);
    }
    
    return result;
}

int handle_create_request(StorageServerState* state, const char* filepath) {
    return create_file(state, filepath);
}

int handle_delete_request(StorageServerState* state, const char* filepath) {
    return delete_file(state, filepath);
}

int handle_copy_request(StorageServerState* state, const char* filepath,
                        const char* dest_ss_ip, int dest_ss_port) {
    return copy_file_to_ss(state, filepath, dest_ss_ip, dest_ss_port);
}

int handle_info_request(StorageServerState* state, int client_fd,
                        const char* filepath) {
    if (!state || !filepath) return ERR_INVALID_OPERATION;
    
    FileEntry* entry = find_file(state, filepath);
    if (!entry) {
        send_all(client_fd, "ERROR:FILE_NOT_FOUND\n", 21);
        return ERR_FILE_NOT_FOUND;
    }
    
    char info[512];
    snprintf(info, sizeof(info),
             "SUCCESS\n"
             "PATH:%s\n"
             "SIZE:%ld\n"
             "SENTENCES:%d\n"
             "CREATED:%ld\n"
             "MODIFIED:%ld\n"
             "IS_DIR:%d\n",
             entry->filepath,
             entry->file_size,
             entry->sentence_count,
             entry->created_at,
             entry->modified_at,
             entry->is_directory);
    
    send_all(client_fd, info, strlen(info));
    
    log_message("SS", "client", client_fd, "user", "INFO", filepath, "SUCCESS");
    
    return ERR_SUCCESS;
}
