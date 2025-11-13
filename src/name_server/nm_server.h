#ifndef NM_SERVER_H
#define NM_SERVER_H

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define MAX_STORAGE_SERVERS 10
#define MAX_CLIENTS 100
#define MAX_FILES 10000
#define NM_PORT 8000
#define MAX_PATH_LEN 512

// Forward declarations
typedef struct StorageServer StorageServer;
typedef struct ClientInfo ClientInfo;
typedef struct FileMetadata FileMetadata;

// ==================== Storage Server Registry ====================

typedef struct StorageServer {
    int ss_id;
    char ip[16];
    int nm_port;              // Port for NM communication
    int client_port;          // Port for client communication
    bool is_active;
    time_t last_heartbeat;
    int file_count;
    char** file_list;         // Dynamic array of filenames
    int sockfd;               // Socket connection to NM
} StorageServer;

// ==================== Client Registry ====================

typedef struct ClientInfo {
    int client_fd;
    char username[64];
    char ip[16];
    int port;
    time_t connected_at;
    bool is_active;
} ClientInfo;

// ==================== Access Control List ====================

typedef struct AccessControlEntry {
    char username[64];
    bool can_read;
    bool can_write;
    struct AccessControlEntry* next;
} AccessControlEntry;

// ==================== File Metadata ====================

typedef struct FileMetadata {
    char filename[256];
    char filepath[MAX_PATH_LEN];  // Full path on SS
    char owner[64];
    int ss_id;                    // Which SS stores this file
    time_t created_at;
    time_t last_modified;
    time_t last_accessed;
    long file_size;
    int word_count;
    int char_count;
    AccessControlEntry* acl_head;  // Linked list of ACLs
} FileMetadata;

// ==================== Name Server State ====================

typedef struct {
    int server_fd;                          // Main server socket
    StorageServer ss_registry[MAX_STORAGE_SERVERS];
    int ss_count;
    pthread_mutex_t ss_mutex;
    
    ClientInfo client_registry[MAX_CLIENTS];
    int client_count;
    pthread_mutex_t client_mutex;
    
    FileMetadata* file_map;                 // Will be replaced by Trie/HashMap
    int file_count;
    pthread_mutex_t file_mutex;
    
    bool running;
} NameServerState;

// ==================== Function Declarations ====================

// Initialization
int nm_init(NameServerState* state);
void nm_cleanup(NameServerState* state);

// Storage Server Management
int register_storage_server(NameServerState* state, int sockfd);
int handle_ss_message(NameServerState* state, int ss_id);
StorageServer* find_storage_server(NameServerState* state, int ss_id);
StorageServer* find_ss_for_file(NameServerState* state, const char* filename);
void update_ss_heartbeat(NameServerState* state, int ss_id);

// Client Management
int register_client(NameServerState* state, int client_fd, const char* username);
int handle_client_request(NameServerState* state, int client_fd);
ClientInfo* find_client(NameServerState* state, int client_fd);
void remove_client(NameServerState* state, int client_fd);

// File Management
int add_file_to_registry(NameServerState* state, FileMetadata* file);
FileMetadata* find_file(NameServerState* state, const char* filename);
int remove_file_from_registry(NameServerState* state, const char* filename);
void update_file_metadata(NameServerState* state, const char* filename, FileMetadata* updated);

// Access Control
bool check_read_permission(FileMetadata* file, const char* username);
bool check_write_permission(FileMetadata* file, const char* username);
int add_access(FileMetadata* file, const char* username, bool read, bool write);
int remove_access(FileMetadata* file, const char* username);

// Request Routing
int route_read_request(NameServerState* state, int client_fd, const char* filename);
int route_write_request(NameServerState* state, int client_fd, const char* filename, int sentence_idx);
int route_create_request(NameServerState* state, int client_fd, const char* filename, const char* owner);
int route_delete_request(NameServerState* state, int client_fd, const char* filename);

// Main server loop
void* nm_server_loop(void* arg);
void* handle_connections(void* arg);

#endif // NM_SERVER_H
