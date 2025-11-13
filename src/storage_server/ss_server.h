#ifndef SS_SERVER_H
#define SS_SERVER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define MAX_FILES 10000
#define MAX_SENTENCE_LOCKS 1000
#define MAX_PATH_LEN 512
#define MAX_SENTENCE_LEN 4096
#define SENTENCE_DELIMITERS ".!?"

// Forward declarations
typedef struct StorageServerState StorageServerState;
typedef struct FileEntry FileEntry;
typedef struct SentenceLock SentenceLock;

/**
 * Sentence Lock Structure
 * Manages sentence-level locking for concurrent writes
 */
typedef struct SentenceLock {
    char filepath[MAX_PATH_LEN];
    int sentence_idx;           // Which sentence is locked
    int client_fd;              // Which client holds the lock
    bool is_write_lock;         // true for write, false for read
    int read_count;             // Number of concurrent readers
    time_t acquired_at;         // When lock was acquired
    pthread_mutex_t lock_mutex; // Mutex for this lock
    struct SentenceLock* next;  // Linked list
} SentenceLock;

/**
 * File Entry Structure
 * Represents a file stored on this storage server
 */
typedef struct FileEntry {
    char filepath[MAX_PATH_LEN];     // Relative path (e.g., "docs/file.txt")
    char full_path[MAX_PATH_LEN];    // Absolute path on disk
    off_t file_size;                 // File size in bytes
    time_t created_at;               // Creation timestamp
    time_t modified_at;              // Last modification timestamp
    int sentence_count;              // Number of sentences in file
    bool is_directory;               // true if directory
    SentenceLock* locks;             // Linked list of sentence locks
    pthread_mutex_t file_mutex;      // Mutex for this file
} FileEntry;

/**
 * Storage Server State
 * Main state structure for the storage server
 */
typedef struct StorageServerState {
    int ss_id;                       // Storage server ID
    char base_path[MAX_PATH_LEN];    // Base directory for storage
    
    // Network configuration
    int nm_port;                     // Name server port
    char nm_ip[16];                  // Name server IP
    int client_port;                 // Port for client connections
    int ss_port;                     // Port for SS-to-SS connections
    
    // Socket descriptors
    int nm_socket;                   // Connection to name server
    int client_listen_socket;        // Listening socket for clients
    int ss_listen_socket;            // Listening socket for other SS
    
    // File registry
    FileEntry files[MAX_FILES];      // Array of file entries
    int file_count;                  // Number of files
    pthread_mutex_t registry_mutex;  // Mutex for file registry
    
    // Lock management
    SentenceLock* active_locks;      // Linked list of active locks
    pthread_mutex_t lock_list_mutex; // Mutex for lock list
    
    // Server state
    bool running;                    // Server running flag
    pthread_t heartbeat_thread;      // Heartbeat thread handle
    
} StorageServerState;

/* ===============================================
 * INITIALIZATION FUNCTIONS
 * =============================================== */

/**
 * Initialize storage server state
 * @param state Pointer to state structure
 * @param ss_id Storage server ID
 * @param base_path Base directory for file storage
 * @param nm_ip Name server IP address
 * @param nm_port Name server port
 * @param client_port Port for client connections
 * @param ss_port Port for SS-to-SS connections
 * @return 0 on success, -1 on failure
 */
int ss_init(StorageServerState* state, int ss_id, const char* base_path,
            const char* nm_ip, int nm_port, int client_port, int ss_port);

/**
 * Register with name server
 * Sends SS_ID, ports, and initial accessible paths
 * @param state Storage server state
 * @return 0 on success, -1 on failure
 */
int register_with_name_server(StorageServerState* state);

/**
 * Send heartbeat to name server periodically
 * @param arg Pointer to StorageServerState
 * @return NULL
 */
void* heartbeat_thread_func(void* arg);

/**
 * Cleanup and shutdown storage server
 * @param state Storage server state
 */
void ss_cleanup(StorageServerState* state);

/* ===============================================
 * FILE OPERATIONS
 * =============================================== */

/**
 * Scan base directory and register all files
 * @param state Storage server state
 * @return Number of files found, -1 on error
 */
int scan_and_register_files(StorageServerState* state);

/**
 * Add a file to the registry
 * @param state Storage server state
 * @param filepath Relative filepath
 * @param is_directory Whether it's a directory
 * @return Pointer to FileEntry, NULL on failure
 */
FileEntry* add_file_to_registry(StorageServerState* state, 
                                 const char* filepath, bool is_directory);

/**
 * Find a file in the registry
 * @param state Storage server state
 * @param filepath Relative filepath
 * @return Pointer to FileEntry, NULL if not found
 */
FileEntry* find_file(StorageServerState* state, const char* filepath);

/**
 * Create a new file
 * @param state Storage server state
 * @param filepath Relative filepath
 * @return 0 on success, error code on failure
 */
int create_file(StorageServerState* state, const char* filepath);

/**
 * Delete a file
 * @param state Storage server state
 * @param filepath Relative filepath
 * @return 0 on success, error code on failure
 */
int delete_file(StorageServerState* state, const char* filepath);

/**
 * Copy a file to another storage server
 * @param state Storage server state
 * @param filepath Source filepath
 * @param dest_ss_ip Destination SS IP
 * @param dest_ss_port Destination SS port
 * @return 0 on success, error code on failure
 */
int copy_file_to_ss(StorageServerState* state, const char* filepath,
                    const char* dest_ss_ip, int dest_ss_port);

/* ===============================================
 * SENTENCE OPERATIONS
 * =============================================== */

/**
 * Count sentences in a file
 * Sentences are delimited by . ! ?
 * @param filepath Full path to file
 * @return Number of sentences, -1 on error
 */
int count_sentences(const char* filepath);

/**
 * Read a specific sentence from a file
 * @param filepath Full path to file
 * @param sentence_idx Sentence index (0-based)
 * @param buffer Buffer to store sentence
 * @param buffer_size Size of buffer
 * @return Number of bytes read, -1 on error
 */
int read_sentence(const char* filepath, int sentence_idx, 
                  char* buffer, size_t buffer_size);

/**
 * Write/modify a specific sentence in a file
 * @param filepath Full path to file
 * @param sentence_idx Sentence index (0-based)
 * @param content New sentence content
 * @return 0 on success, error code on failure
 */
int write_sentence(const char* filepath, int sentence_idx, const char* content);

/**
 * Append content to a file
 * @param filepath Full path to file
 * @param content Content to append
 * @return 0 on success, error code on failure
 */
int append_to_file(const char* filepath, const char* content);

/* ===============================================
 * LOCKING MECHANISMS
 * =============================================== */

/**
 * Acquire a read lock on a sentence
 * Multiple readers can hold read locks simultaneously
 * @param state Storage server state
 * @param filepath File path
 * @param sentence_idx Sentence index
 * @param client_fd Client file descriptor
 * @return 0 on success, error code on failure
 */
int acquire_read_lock(StorageServerState* state, const char* filepath,
                      int sentence_idx, int client_fd);

/**
 * Acquire a write lock on a sentence
 * Exclusive lock - no other readers or writers
 * @param state Storage server state
 * @param filepath File path
 * @param sentence_idx Sentence index
 * @param client_fd Client file descriptor
 * @return 0 on success, error code on failure
 */
int acquire_write_lock(StorageServerState* state, const char* filepath,
                       int sentence_idx, int client_fd);

/**
 * Release a lock on a sentence
 * @param state Storage server state
 * @param filepath File path
 * @param sentence_idx Sentence index
 * @param client_fd Client file descriptor
 * @return 0 on success, error code on failure
 */
int release_lock(StorageServerState* state, const char* filepath,
                 int sentence_idx, int client_fd);

/**
 * Release all locks held by a client (e.g., on disconnect)
 * @param state Storage server state
 * @param client_fd Client file descriptor
 * @return Number of locks released
 */
int release_all_locks_for_client(StorageServerState* state, int client_fd);

/**
 * Check if a sentence is locked
 * @param state Storage server state
 * @param filepath File path
 * @param sentence_idx Sentence index
 * @return true if locked, false otherwise
 */
bool is_sentence_locked(StorageServerState* state, const char* filepath,
                        int sentence_idx);

/* ===============================================
 * REQUEST HANDLERS
 * =============================================== */

/**
 * Handle READ request from client
 * @param state Storage server state
 * @param client_fd Client socket
 * @param filepath File to read
 * @return 0 on success, error code on failure
 */
int handle_read_request(StorageServerState* state, int client_fd, 
                        const char* filepath);

/**
 * Handle WRITE request from client
 * @param state Storage server state
 * @param client_fd Client socket
 * @param filepath File to write
 * @param sentence_idx Sentence to modify
 * @param content New content
 * @return 0 on success, error code on failure
 */
int handle_write_request(StorageServerState* state, int client_fd,
                         const char* filepath, int sentence_idx, 
                         const char* content);

/**
 * Handle CREATE request from name server
 * @param state Storage server state
 * @param filepath File to create
 * @return 0 on success, error code on failure
 */
int handle_create_request(StorageServerState* state, const char* filepath);

/**
 * Handle DELETE request from name server
 * @param state Storage server state
 * @param filepath File to delete
 * @return 0 on success, error code on failure
 */
int handle_delete_request(StorageServerState* state, const char* filepath);

/**
 * Handle COPY request from name server
 * @param state Storage server state
 * @param filepath File to copy
 * @param dest_ss_ip Destination SS IP
 * @param dest_ss_port Destination SS port
 * @return 0 on success, error code on failure
 */
int handle_copy_request(StorageServerState* state, const char* filepath,
                        const char* dest_ss_ip, int dest_ss_port);

/**
 * Handle INFO request (file metadata)
 * @param state Storage server state
 * @param client_fd Client socket
 * @param filepath File path
 * @return 0 on success, error code on failure
 */
int handle_info_request(StorageServerState* state, int client_fd,
                        const char* filepath);

#endif // SS_SERVER_H
