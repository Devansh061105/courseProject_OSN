# Docs++ Distributed File System - Detailed Implementation Plan

**Project**: OSN Course Project - Distributed Document Collaboration System  
**Deadline**: November 18, 2025, 11:59 PM IST  
**Time Available**: 10 days  
**Total Marks**: 200 (Base) + 50 (Bonus) = 250

---

## 📋 Executive Summary

Build a distributed file system similar to Google Docs with:
- **Name Server (NM)**: Central coordinator managing file locations and user access
- **Storage Servers (SS)**: Multiple nodes storing actual file data
- **Clients**: Multiple concurrent users performing file operations

**Key Challenge**: Concurrent access with sentence-level locking, efficient search (< O(N)), and data persistence.

---

## 🏗️ System Architecture

```
┌──────────────┐
│   Client 1   │──┐
└──────────────┘  │
                  │         ┌─────────────────┐
┌──────────────┐  ├────────▶│  Name Server    │
│   Client 2   │──┤         │  (Coordinator)  │
└──────────────┘  │         └─────────────────┘
                  │              │         │
┌──────────────┐  │              ▼         ▼
│   Client N   │──┘         ┌─────┐   ┌─────┐
└──────────────┘            │ SS1 │   │ SS2 │
                            └─────┘   └─────┘
```

### Communication Patterns:
1. **Client → NM**: All requests start here
2. **NM → SS**: File operations (CREATE, DELETE)
3. **Client ↔ SS**: Direct for READ, WRITE, STREAM
4. **SS → NM**: Registration, heartbeat, ACK

---

## 📁 Project Directory Structure

```
course-project/
├── src/
│   ├── common/
│   │   ├── protocol.h          # Message structures & command codes
│   │   ├── protocol.c
│   │   ├── error_codes.h       # Universal error code definitions
│   │   ├── logger.h            # Logging utilities
│   │   ├── logger.c
│   │   ├── utils.h             # Socket helpers, string utilities
│   │   └── utils.c
│   │
│   ├── name_server/
│   │   ├── main.c              # Entry point
│   │   ├── nm_server.h         # Main server logic
│   │   ├── nm_server.c
│   │   ├── file_manager.h      # File-to-SS mapping
│   │   ├── file_manager.c
│   │   ├── search_cache.h      # Trie/HashMap + LRU cache
│   │   ├── search_cache.c
│   │   ├── access_control.h    # User permissions & ACL
│   │   └── access_control.c
│   │
│   ├── storage_server/
│   │   ├── main.c              # Entry point
│   │   ├── ss_server.h         # Main server logic
│   │   ├── ss_server.c
│   │   ├── file_ops.h          # File I/O operations
│   │   ├── file_ops.c
│   │   ├── sentence_parser.h   # Parse text into sentences
│   │   ├── sentence_parser.c
│   │   ├── lock_manager.h      # Sentence-level locking
│   │   ├── lock_manager.c
│   │   ├── undo_manager.h      # Single-level undo
│   │   └── undo_manager.c
│   │
│   └── client/
│       ├── main.c              # Entry point
│       ├── client.h            # Client logic
│       ├── client.c
│       ├── commands.h          # Command parsing
│       └── commands.c
│
├── data/                       # Storage Server data
│   ├── ss1/
│   │   ├── files/             # Actual file content
│   │   └── metadata/          # .meta files
│   └── ss2/
│       ├── files/
│       └── metadata/
│
├── logs/                       # Log files
│   ├── name_server.log
│   ├── ss1.log
│   └── ss2.log
│
├── tests/                      # Test scripts
│   ├── basic_test.sh
│   ├── concurrent_test.sh
│   └── stress_test.sh
│
├── Makefile                    # Build system
├── README.md                   # Setup & usage instructions
├── IMPLEMENTATION_PLAN.md      # This file
└── question.md                 # Original requirements
```

---

## 🔧 Implementation Phases

### **Phase 1: Foundation (Days 1-3)** ⏱️ 3 days

#### 1.1 Setup Development Environment
- [x] Move project to WSL
- [ ] Install required tools: `gcc`, `make`, `valgrind`, `gdb`, `netcat`
- [ ] Setup Git repository
- [ ] Create directory structure

#### 1.2 Common Module Implementation

**protocol.h - Message Format**:
```c
#define MAX_FILENAME 256
#define MAX_USERNAME 64
#define MAX_MESSAGE 4096

// Command codes
typedef enum {
    CMD_VIEW = 1,
    CMD_READ = 2,
    CMD_CREATE = 3,
    CMD_WRITE = 4,
    CMD_UNDO = 5,
    CMD_INFO = 6,
    CMD_DELETE = 7,
    CMD_STREAM = 8,
    CMD_LIST = 9,
    CMD_ADDACCESS = 10,
    CMD_REMACCESS = 11,
    CMD_EXEC = 12
} CommandCode;

// Request structure
typedef struct {
    CommandCode cmd;
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
    int sentence_index;
    char flags[10];
    char data[MAX_MESSAGE];
} Request;

// Response structure
typedef struct {
    int status_code;    // 0 = success, >0 = error
    char message[MAX_MESSAGE];
    char ss_ip[16];     // For redirect to SS
    int ss_port;
} Response;
```

**error_codes.h - Error Definitions**:
```c
#define SUCCESS 0
#define ERR_FILE_NOT_FOUND 1
#define ERR_UNAUTHORIZED_ACCESS 2
#define ERR_FILE_LOCKED 3
#define ERR_SENTENCE_OUT_OF_RANGE 4
#define ERR_WORD_OUT_OF_RANGE 5
#define ERR_SS_UNAVAILABLE 6
#define ERR_INVALID_COMMAND 7
#define ERR_FILE_ALREADY_EXISTS 8
#define ERR_PERMISSION_DENIED 9
#define ERR_INVALID_USERNAME 10
#define ERR_CONNECTION_FAILED 11
#define ERR_NETWORK_ERROR 12
```

**logger.c - Logging Implementation**:
```c
void log_message(const char* component, const char* ip, int port, 
                 const char* username, const char* operation, 
                 const char* result);
// Format: [2025-11-08 14:32:15] [NM] [192.168.1.100:5001] [user1] READ file.txt - SUCCESS
```

**utils.c - Socket Utilities**:
```c
int create_server_socket(int port);
int connect_to_server(const char* ip, int port);
int send_message(int sockfd, const void* data, size_t len);
int recv_message(int sockfd, void* data, size_t len);
```

#### 1.3 Makefile Setup
```makefile
CC = gcc
CFLAGS = -Wall -Wextra -pthread -g -O2
LDFLAGS = -lpthread

COMMON_OBJS = src/common/protocol.o src/common/logger.o src/common/utils.o

all: name_server storage_server client

name_server: $(COMMON_OBJS) src/name_server/*.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

storage_server: $(COMMON_OBJS) src/storage_server/*.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: $(COMMON_OBJS) src/client/*.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f name_server storage_server client
	rm -f src/*/*.o
	rm -rf logs/* data/*/files/* data/*/metadata/*

test: all
	bash tests/basic_test.sh
```

---

### **Phase 2: Name Server (Days 4-6)** ⏱️ 3 days

#### 2.1 Core Data Structures

**File Metadata**:
```c
typedef struct AccessControlEntry {
    char username[MAX_USERNAME];
    bool can_read;
    bool can_write;
    struct AccessControlEntry* next;
} AccessControlEntry;

typedef struct {
    char filename[MAX_FILENAME];
    char owner[MAX_USERNAME];
    int ss_id;                      // Which SS stores this
    time_t created_at;
    time_t last_modified;
    time_t last_accessed;
    size_t file_size;
    int word_count;
    int char_count;
    AccessControlEntry* acl_head;   // Linked list of ACLs
} FileMetadata;
```

**Efficient Search - Trie Implementation**:
```c
typedef struct TrieNode {
    struct TrieNode* children[128];  // ASCII characters
    FileMetadata* file_info;
    bool is_end_of_filename;
} TrieNode;

TrieNode* create_trie();
void insert_file(TrieNode* root, const char* filename, FileMetadata* meta);
FileMetadata* search_file(TrieNode* root, const char* filename);
void delete_file_from_trie(TrieNode* root, const char* filename);
```

**LRU Cache**:
```c
#define CACHE_SIZE 500

typedef struct CacheNode {
    char filename[MAX_FILENAME];
    FileMetadata* data;
    struct CacheNode *prev, *next;
} CacheNode;

typedef struct {
    CacheNode* head;
    CacheNode* tail;
    int size;
    CacheNode* cache[CACHE_SIZE];  // Hash table
} LRUCache;

FileMetadata* get_from_cache(LRUCache* cache, const char* filename);
void put_in_cache(LRUCache* cache, const char* filename, FileMetadata* meta);
```

**Storage Server Registry**:
```c
typedef struct {
    int ss_id;
    char ip[16];
    int nm_port;        // Port for NM communication
    int client_port;    // Port for client communication
    bool is_active;
    time_t last_heartbeat;
    int file_count;
    char** file_list;   // Array of filenames on this SS
} StorageServerInfo;

StorageServerInfo ss_registry[MAX_STORAGE_SERVERS];
```

#### 2.2 NM Operations Implementation

**Initialization**:
```c
void init_name_server() {
    // 1. Create server socket on port 8000
    // 2. Initialize Trie for file mapping
    // 3. Initialize LRU cache
    // 4. Create thread pool for handling clients
    // 5. Start listening for SS and client connections
}
```

**Request Routing Logic**:
```c
void handle_client_request(int client_fd, Request* req) {
    switch(req->cmd) {
        case CMD_READ:
        case CMD_WRITE:
        case CMD_STREAM:
            // Return SS info to client
            find_ss_for_file(req->filename);
            send_ss_info_to_client(client_fd);
            break;
            
        case CMD_CREATE:
        case CMD_DELETE:
            // Forward to SS and relay response
            forward_to_ss(req);
            wait_for_ss_ack();
            send_response_to_client(client_fd);
            break;
            
        case CMD_VIEW:
        case CMD_INFO:
        case CMD_LIST:
        case CMD_ADDACCESS:
        case CMD_REMACCESS:
            // Handle locally
            process_locally(req);
            send_response_to_client(client_fd);
            break;
            
        case CMD_EXEC:
            // Fetch from SS, execute, return output
            fetch_file_from_ss(req->filename);
            execute_commands();
            send_output_to_client(client_fd);
            break;
    }
}
```

#### 2.3 Access Control Implementation

```c
bool check_read_permission(FileMetadata* meta, const char* username) {
    if (strcmp(meta->owner, username) == 0)
        return true;
    
    AccessControlEntry* entry = meta->acl_head;
    while (entry) {
        if (strcmp(entry->username, username) == 0)
            return entry->can_read;
        entry = entry->next;
    }
    return false;
}

bool check_write_permission(FileMetadata* meta, const char* username) {
    if (strcmp(meta->owner, username) == 0)
        return true;
    
    AccessControlEntry* entry = meta->acl_head;
    while (entry) {
        if (strcmp(entry->username, username) == 0)
            return entry->can_write;
        entry = entry->next;
    }
    return false;
}

void add_access(FileMetadata* meta, const char* username, 
                bool read, bool write) {
    AccessControlEntry* new_entry = malloc(sizeof(AccessControlEntry));
    strcpy(new_entry->username, username);
    new_entry->can_read = read;
    new_entry->can_write = write;
    new_entry->next = meta->acl_head;
    meta->acl_head = new_entry;
}
```

---

### **Phase 3: Storage Server (Days 7-9)** ⏱️ 3 days

#### 3.1 File Structure & Persistence

**File Storage Layout**:
```
data/ss1/
├── files/
│   ├── test.txt           # Actual content
│   ├── doc.txt
│   └── report.txt
└── metadata/
    ├── test.meta          # Metadata JSON
    ├── doc.meta
    ├── report.meta
    └── backups/
        ├── test_undo.txt  # Undo backup
        ├── doc_undo.txt
        └── report_undo.txt
```

**Metadata File Format (.meta)**:
```json
{
  "filename": "test.txt",
  "owner": "user1",
  "created_at": "2025-11-08T10:30:00Z",
  "last_modified": "2025-11-08T14:45:00Z",
  "last_accessed": "2025-11-08T15:00:00Z",
  "size_bytes": 1024,
  "word_count": 250,
  "char_count": 1024,
  "sentence_count": 15,
  "has_undo_backup": true
}
```

#### 3.2 Sentence Parsing & Locking

**Sentence Structure**:
```c
typedef struct {
    char* text;             // Sentence content
    int word_count;
    bool is_locked;
    int locked_by_fd;       // Client FD that locked it
    pthread_mutex_t mutex;
} Sentence;

typedef struct {
    Sentence* sentences;
    int sentence_count;
    pthread_rwlock_t file_lock;  // Reader-writer lock for file
} ParsedFile;
```

**Sentence Parser**:
```c
ParsedFile* parse_file_into_sentences(const char* content) {
    // 1. Scan for delimiters: '.', '!', '?'
    // 2. Each delimiter ends current sentence
    // 3. "e.g. test" → ["e.", "g.", " test"]
    // 4. Return array of Sentence structs
}
```

**Lock Manager**:
```c
bool try_lock_sentence(ParsedFile* file, int sentence_idx, int client_fd) {
    if (sentence_idx >= file->sentence_count)
        return false;
    
    pthread_mutex_lock(&file->sentences[sentence_idx].mutex);
    
    if (file->sentences[sentence_idx].is_locked) {
        pthread_mutex_unlock(&file->sentences[sentence_idx].mutex);
        return false;  // Already locked
    }
    
    file->sentences[sentence_idx].is_locked = true;
    file->sentences[sentence_idx].locked_by_fd = client_fd;
    pthread_mutex_unlock(&file->sentences[sentence_idx].mutex);
    return true;
}

void unlock_sentence(ParsedFile* file, int sentence_idx) {
    pthread_mutex_lock(&file->sentences[sentence_idx].mutex);
    file->sentences[sentence_idx].is_locked = false;
    file->sentences[sentence_idx].locked_by_fd = -1;
    pthread_mutex_unlock(&file->sentences[sentence_idx].mutex);
}
```

#### 3.3 WRITE Operation - The Complex One

**WRITE Flow**:
```
Client: WRITE test.txt 1
SS: Lock sentence[1]
Client: 3 beautiful
SS: Parse "beautiful", check for delimiters
SS: Insert at word_index 3
Client: 5 day.
SS: Parse "day.", delimiter found!
SS: Split sentence[1] into sentence[1] and sentence[2]
Client: ETIRW
SS: Save undo backup
SS: Write to file
SS: Update metadata
SS: Unlock (original) sentence[1]
SS: Send ACK
```

**Implementation**:
```c
void handle_write_command(int client_fd, const char* filename, 
                          int sentence_idx) {
    // 1. Load file and parse into sentences
    ParsedFile* file = load_and_parse_file(filename);
    
    // 2. Check sentence index
    if (sentence_idx >= file->sentence_count) {
        send_error(client_fd, ERR_SENTENCE_OUT_OF_RANGE);
        return;
    }
    
    // 3. Try to lock sentence
    if (!try_lock_sentence(file, sentence_idx, client_fd)) {
        send_error(client_fd, ERR_FILE_LOCKED);
        return;
    }
    
    // 4. Save undo backup
    save_undo_backup(filename, file);
    
    // 5. Enter interactive mode
    while (true) {
        char word_update[MAX_MESSAGE];
        recv_message(client_fd, word_update, sizeof(word_update));
        
        if (strcmp(word_update, "ETIRW") == 0)
            break;
        
        // Parse: <word_index> <content>
        int word_idx;
        char content[1024];
        sscanf(word_update, "%d %[^\n]", &word_idx, content);
        
        // Apply update
        apply_word_update(file, sentence_idx, word_idx, content);
        
        // Check for delimiters and split if needed
        if (contains_delimiter(content)) {
            split_sentences_at_delimiter(file, sentence_idx);
            // sentence_idx now refers to the first split sentence
        }
    }
    
    // 6. Write back to file
    write_parsed_file_to_disk(filename, file);
    
    // 7. Update metadata
    update_metadata(filename, file);
    
    // 8. Unlock
    unlock_sentence(file, sentence_idx);
    
    // 9. Send ACK
    send_success(client_fd);
}
```

**Delimiter Handling**:
```c
void split_sentences_at_delimiter(ParsedFile* file, int idx) {
    Sentence* sent = &file->sentences[idx];
    char* delimiters = ".!?";
    
    // Find delimiter positions
    int* delimiter_positions = find_all_delimiters(sent->text);
    int num_delimiters = count_delimiters(sent->text);
    
    if (num_delimiters == 0)
        return;
    
    // Create new sentences array
    int new_count = file->sentence_count + num_delimiters;
    Sentence* new_sentences = malloc(new_count * sizeof(Sentence));
    
    // Copy sentences before idx
    memcpy(new_sentences, file->sentences, idx * sizeof(Sentence));
    
    // Split sentence[idx] into multiple sentences
    int current_pos = 0;
    for (int i = 0; i <= num_delimiters; i++) {
        // Extract substring and create new sentence
    }
    
    // Copy sentences after idx
    memcpy(&new_sentences[idx + num_delimiters + 1], 
           &file->sentences[idx + 1],
           (file->sentence_count - idx - 1) * sizeof(Sentence));
    
    // Update file structure
    free(file->sentences);
    file->sentences = new_sentences;
    file->sentence_count = new_count;
}
```

#### 3.4 Undo Implementation

```c
void save_undo_backup(const char* filename, ParsedFile* file) {
    char backup_path[512];
    snprintf(backup_path, sizeof(backup_path), 
             "data/ss%d/metadata/backups/%s_undo.txt", ss_id, filename);
    
    FILE* fp = fopen(backup_path, "w");
    write_parsed_file_to_fp(fp, file);
    fclose(fp);
}

void handle_undo(const char* filename) {
    char backup_path[512];
    snprintf(backup_path, sizeof(backup_path), 
             "data/ss%d/metadata/backups/%s_undo.txt", ss_id, filename);
    
    if (access(backup_path, F_OK) != 0) {
        send_error(client_fd, ERR_NO_UNDO_AVAILABLE);
        return;
    }
    
    // Copy backup to main file
    char main_path[512];
    snprintf(main_path, sizeof(main_path), 
             "data/ss%d/files/%s", ss_id, filename);
    
    copy_file(backup_path, main_path);
    update_metadata(filename, NULL);
    send_success(client_fd);
}
```

---

### **Phase 4: Client Implementation (Days 10-11)** ⏱️ 2 days

#### 4.1 Client Structure

```c
typedef struct {
    char username[MAX_USERNAME];
    int nm_socket;          // Connection to Name Server
    char nm_ip[16];
    int nm_port;
} ClientContext;
```

#### 4.2 Command Parser

```c
typedef struct {
    CommandCode cmd;
    char filename[MAX_FILENAME];
    int sentence_index;
    char flags[10];
    char target_user[MAX_USERNAME];
} ParsedCommand;

ParsedCommand parse_user_input(const char* input) {
    ParsedCommand cmd;
    
    if (strncmp(input, "VIEW", 4) == 0) {
        cmd.cmd = CMD_VIEW;
        // Parse flags -a, -l, -al
    } else if (strncmp(input, "READ", 4) == 0) {
        cmd.cmd = CMD_READ;
        sscanf(input, "READ %s", cmd.filename);
    } else if (strncmp(input, "WRITE", 5) == 0) {
        cmd.cmd = CMD_WRITE;
        sscanf(input, "WRITE %s %d", cmd.filename, &cmd.sentence_index);
    }
    // ... handle all commands
    
    return cmd;
}
```

#### 4.3 WRITE Interactive Mode

```c
void execute_write_command(ClientContext* ctx, ParsedCommand* cmd) {
    // 1. Send WRITE request to NM
    Request req = {
        .cmd = CMD_WRITE,
        .filename = cmd->filename,
        .sentence_index = cmd->sentence_index
    };
    strcpy(req.username, ctx->username);
    send_message(ctx->nm_socket, &req, sizeof(req));
    
    // 2. Receive SS info from NM
    Response resp;
    recv_message(ctx->nm_socket, &resp, sizeof(resp));
    
    if (resp.status_code != SUCCESS) {
        printf("ERROR: %s\n", resp.message);
        return;
    }
    
    // 3. Connect directly to SS
    int ss_socket = connect_to_server(resp.ss_ip, resp.ss_port);
    
    // 4. Send WRITE command to SS
    send_message(ss_socket, &req, sizeof(req));
    
    // 5. Enter interactive mode
    printf("Enter word updates (format: <word_index> <content>)\n");
    printf("Type ETIRW to finish.\n");
    
    while (true) {
        char line[MAX_MESSAGE];
        fgets(line, sizeof(line), stdin);
        line[strcspn(line, "\n")] = 0;  // Remove newline
        
        send_message(ss_socket, line, strlen(line) + 1);
        
        if (strcmp(line, "ETIRW") == 0)
            break;
    }
    
    // 6. Receive ACK from SS
    Response ss_resp;
    recv_message(ss_socket, &ss_resp, sizeof(ss_resp));
    
    if (ss_resp.status_code == SUCCESS)
        printf("Write Successful!\n");
    else
        printf("ERROR: %s\n", ss_resp.message);
    
    close(ss_socket);
}
```

#### 4.4 STREAM Implementation

```c
void execute_stream_command(ClientContext* ctx, ParsedCommand* cmd) {
    // Similar to READ, but display word by word
    
    // 1. Get SS info from NM
    // 2. Connect to SS
    // 3. Send STREAM request
    
    // 4. Receive and display words
    while (true) {
        char word[256];
        int len = recv_message(ss_socket, word, sizeof(word));
        
        if (len <= 0) {
            printf("\nERROR: Connection lost to Storage Server\n");
            break;
        }
        
        if (strcmp(word, "STOP") == 0)
            break;
        
        printf("%s ", word);
        fflush(stdout);
        
        usleep(100000);  // 0.1 second delay
    }
    printf("\n");
}
```

---

### **Phase 5: User Functionalities (Days 12-14)** ⏱️ 3 days

#### Priority Implementation Order:

**Day 12: Basic Operations (40 marks)**
1. ✅ VIEW [10 marks]
   - Parse flags: -a (all files), -l (details), -al (both)
   - Query NM for file list
   - Format output as table
   
2. ✅ READ [10 marks]
   - Get SS info from NM
   - Connect to SS
   - Receive and display full file content
   
3. ✅ CREATE [10 marks]
   - Send CREATE to NM
   - NM selects SS (round-robin or least loaded)
   - SS creates empty file
   - Update NM's file mapping
   
4. ✅ INFO [10 marks]
   - Query NM for metadata
   - Display formatted output

**Day 13: Core Features (70 marks)**
5. ✅ WRITE [30 marks] - Most complex
   - Already detailed above
   - Test thoroughly!
   
6. ✅ DELETE [10 marks]
   - Owner check
   - NM forwards to SS
   - SS removes file + metadata
   - NM updates mapping and ACLs
   
7. ✅ UNDO [15 marks]
   - Get SS from NM
   - Connect to SS
   - SS restores from backup
   
8. ✅ LIST [10 marks]
   - NM maintains user registry
   - Return list of all connected users
   
9. ✅ STREAM [15 marks]
   - Word-by-word with 0.1s delay
   - Handle SS disconnect

**Day 14: Advanced Features (40 marks)**
10. ✅ ADDACCESS/REMACCESS [15 marks]
    - Owner verification
    - Update ACL in NM
    - Persist ACL changes
    
11. ✅ EXEC [15 marks]
    - NM fetches file from SS
    - Execute with popen()
    - Capture stdout/stderr
    - Send to client
    - Add timeout (e.g., 10 seconds)

---

### **Phase 6: System Requirements (Days 15-16)** ⏱️ 2 days

#### 6.1 Data Persistence [10 marks]
```c
// On SS startup
void load_persistent_data() {
    // 1. Scan data/ss{id}/files/ directory
    // 2. For each file, load corresponding .meta
    // 3. Send file list to NM during registration
}

// On file modification
void persist_metadata(const char* filename, FileMetadata* meta) {
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), 
             "data/ss%d/metadata/%s.meta", ss_id, filename);
    
    FILE* fp = fopen(meta_path, "w");
    fprintf(fp, "{\n");
    fprintf(fp, "  \"filename\": \"%s\",\n", meta->filename);
    fprintf(fp, "  \"owner\": \"%s\",\n", meta->owner);
    fprintf(fp, "  \"created_at\": \"%ld\",\n", meta->created_at);
    // ... write all fields
    fprintf(fp, "}\n");
    fclose(fp);
}
```

#### 6.2 Logging [5 marks]
```c
void log_request(const char* component, const char* ip, int port,
                 const char* username, const char* operation,
                 const char* details, const char* result) {
    FILE* log_file = fopen("logs/name_server.log", "a");
    
    char timestamp[64];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", 
             localtime(&now));
    
    fprintf(log_file, "[%s] [%s] [%s:%d] [%s] %s - %s - %s\n",
            timestamp, component, ip, port, username, 
            operation, details, result);
    
    // Also print to terminal
    printf("[%s] [%s] [%s:%d] [%s] %s - %s\n",
           timestamp, component, ip, port, username, operation, result);
    
    fclose(log_file);
}

// Usage:
log_request("NM", client_ip, client_port, "user1", 
            "READ", "test.txt", "SUCCESS");
log_request("SS1", client_ip, client_port, "user2", 
            "WRITE", "doc.txt sentence 3", "LOCKED");
```

#### 6.3 Error Handling [5 marks]
```c
const char* get_error_message(int error_code) {
    switch(error_code) {
        case ERR_FILE_NOT_FOUND:
            return "File not found in the system.";
        case ERR_UNAUTHORIZED_ACCESS:
            return "You do not have permission to access this file.";
        case ERR_FILE_LOCKED:
            return "The sentence is currently locked by another user.";
        case ERR_SENTENCE_OUT_OF_RANGE:
            return "Sentence index out of range.";
        case ERR_WORD_OUT_OF_RANGE:
            return "Word index out of range.";
        case ERR_SS_UNAVAILABLE:
            return "Storage server is currently unavailable.";
        default:
            return "Unknown error occurred.";
    }
}
```

#### 6.4 Efficient Search [15 marks]
- Already implemented in Phase 2 with Trie + LRU Cache
- **Complexity Analysis**:
  - Trie search: O(L) where L = filename length
  - HashMap exact match: O(1) average case
  - LRU cache: O(1) for cache hit
  - **Much better than O(N)** ✅

---

### **Phase 7: Testing & Integration (Days 17-18)** ⏱️ 2 days

#### Test Suite

**basic_test.sh**:
```bash
#!/bin/bash

echo "=== Basic Functionality Test ==="

# Start servers
./name_server &
NM_PID=$!
sleep 1

./storage_server 1 &
SS1_PID=$!
sleep 1

# Test CREATE
echo "Testing CREATE..."
echo "CREATE test.txt" | ./client user1 | grep "success"

# Test WRITE
echo "Testing WRITE..."
(echo "WRITE test.txt 0"; echo "1 Hello world."; echo "ETIRW") | ./client user1

# Test READ
echo "Testing READ..."
echo "READ test.txt" | ./client user1 | grep "Hello world"

# Test VIEW
echo "Testing VIEW..."
echo "VIEW" | ./client user1 | grep "test.txt"

# Cleanup
kill $NM_PID $SS1_PID
echo "Basic tests completed!"
```

**concurrent_test.sh**:
```bash
#!/bin/bash

echo "=== Concurrent Access Test ==="

# Start servers
./name_server &
NM_PID=$!
sleep 1

./storage_server 1 &
SS1_PID=$!
sleep 1

# Create file with multiple sentences
(echo "CREATE multi.txt"; 
 echo "WRITE multi.txt 0"; 
 echo "1 Sentence one."; 
 echo "ETIRW"; 
 echo "WRITE multi.txt 1"; 
 echo "1 Sentence two."; 
 echo "ETIRW") | ./client user1

# Test 1: Two clients read simultaneously (should work)
echo "Test 1: Concurrent reads..."
echo "READ multi.txt" | ./client user2 &
echo "READ multi.txt" | ./client user3 &
wait

# Test 2: Two clients write different sentences (should work)
echo "Test 2: Concurrent writes to different sentences..."
(echo "WRITE multi.txt 0"; echo "1 Updated"; echo "ETIRW") | ./client user1 &
(echo "WRITE multi.txt 1"; echo "1 Modified"; echo "ETIRW") | ./client user2 &
wait

# Test 3: Two clients write same sentence (one should fail)
echo "Test 3: Concurrent writes to same sentence..."
(echo "WRITE multi.txt 0"; sleep 2; echo "1 First"; echo "ETIRW") | ./client user1 &
sleep 0.5
(echo "WRITE multi.txt 0"; echo "1 Second"; echo "ETIRW") | ./client user2 | grep "LOCKED"
wait

kill $NM_PID $SS1_PID
echo "Concurrent tests completed!"
```

**stress_test.sh**:
```bash
#!/bin/bash

echo "=== Stress Test ==="

# Start servers
./name_server &
NM_PID=$!
sleep 1

./storage_server 1 &
./storage_server 2 &
sleep 1

# Create 1000 files
echo "Creating 1000 files..."
for i in {1..1000}; do
    echo "CREATE file$i.txt" | ./client user1 > /dev/null &
    if [ $((i % 100)) -eq 0 ]; then
        wait
        echo "Created $i files..."
    fi
done
wait

# Test search performance
echo "Testing search performance..."
start_time=$(date +%s%N)
for i in {1..100}; do
    echo "INFO file$((RANDOM % 1000 + 1)).txt" | ./client user1 > /dev/null
done
end_time=$(date +%s%N)
elapsed=$((($end_time - $start_time) / 1000000))
echo "100 searches took ${elapsed}ms (avg: $((elapsed / 100))ms per search)"

# Spawn 50 concurrent clients
echo "Testing 50 concurrent clients..."
for i in {1..50}; do
    (echo "VIEW"; echo "READ file1.txt") | ./client user$i > /dev/null &
done
wait

echo "Stress tests completed!"
```

#### Memory Leak Check
```bash
valgrind --leak-check=full --show-leak-kinds=all ./name_server
valgrind --leak-check=full --show-leak-kinds=all ./storage_server 1
valgrind --leak-check=full --show-leak-kinds=all ./client user1
```

---

### **Phase 8: Bonus Features (Days 19-20)** ⏱️ Optional

#### Bonus 1: Fault Tolerance [15 marks] - RECOMMENDED
- Replicate each file to 2 Storage Servers
- Async replication (don't wait for ACK)
- Heartbeat mechanism for SS failure detection
- Automatic failover to replica on SS failure
- Re-sync on SS recovery

#### Bonus 2: Hierarchical Folders [10 marks]
- Implement folder tree structure
- Commands: CREATEFOLDER, MOVE, VIEWFOLDER
- Path-based file identification: `/folder1/subfolder/file.txt`

#### Bonus 3: Checkpoints [15 marks]
- Save named snapshots of file state
- Commands: CHECKPOINT, VIEWCHECKPOINT, REVERT, LISTCHECKPOINTS
- Store in separate directory

---

## 🔥 Critical Implementation Notes

### 1. WRITE Command Delimiter Handling
```
Input: "Hello. World"
Result: Two sentences: ["Hello.", " World"]

Input: "e.g. example"
Result: Three sentences: ["e.", "g.", " example"]
```
**Every period/!/? creates a new sentence, no exceptions!**

### 2. Sentence Locking
- Lock only the sentence being edited
- Other sentences in same file can be edited simultaneously
- Use pthread_mutex for each sentence

### 3. EXEC Security
- **MUST execute on Name Server**, not client
- Add timeout to prevent infinite loops
- Consider sandboxing (optional)

### 4. Direct SS Communication
For READ, WRITE, STREAM:
1. Client → NM: "I want to READ file.txt"
2. NM → Client: "Connect to SS at 192.168.1.10:9000"
3. Client → SS: Direct connection for data transfer

### 5. Error Codes Must Be Universal
All components must use same error code numbers.

---

## 📊 Grading Breakdown

| Component | Marks | Priority |
|-----------|-------|----------|
| VIEW | 10 | High |
| READ | 10 | High |
| CREATE | 10 | High |
| INFO | 10 | High |
| WRITE | 30 | **CRITICAL** |
| UNDO | 15 | High |
| STREAM | 15 | Medium |
| LIST | 10 | Low |
| DELETE | 10 | Medium |
| ACCESS CONTROL | 15 | Medium |
| EXEC | 15 | Medium |
| **User Total** | **150** | |
| Persistence | 10 | High |
| Access Control (system) | 5 | High |
| Logging | 5 | Medium |
| Error Handling | 5 | Medium |
| Efficient Search | 15 | High |
| **System Total** | **40** | |
| Specifications | 10 | Medium |
| **Base Total** | **200** | |
| Bonus Features | 50 | Optional |
| **Grand Total** | **250** | |

---

## ⚠️ Common Pitfalls & Solutions

### Pitfall 1: Deadlock in Sentence Locking
**Problem**: Client A locks sentence 1, Client B locks sentence 2, both try to merge.
**Solution**: Always lock in ascending order of sentence indices.

### Pitfall 2: Race Condition in WRITE
**Problem**: Sentence splits during WRITE, indices become invalid.
**Solution**: Process all updates in single critical section, update indices dynamically.

### Pitfall 3: Memory Leaks
**Problem**: Forgot to free dynamically allocated sentences after split.
**Solution**: Use valgrind, implement proper cleanup functions.

### Pitfall 4: Buffer Overflow
**Problem**: Long filenames or content overflow fixed buffers.
**Solution**: Use strncpy, validate input lengths.

### Pitfall 5: Network Timeout
**Problem**: Client waits forever if SS crashes.
**Solution**: Set socket timeouts with setsockopt(SO_RCVTIMEO).

---

## 🎯 Success Criteria

- [ ] All 11 user commands working
- [ ] Concurrent access with sentence locking
- [ ] Data persists across SS restart
- [ ] Search is O(L) or better (not O(N))
- [ ] No memory leaks (valgrind clean)
- [ ] Comprehensive logging
- [ ] Clear error messages
- [ ] Compiles with no warnings
- [ ] Handles 100+ concurrent clients
- [ ] Handles 1000+ files efficiently

---

## 📚 Resources

1. **Beej's Guide to Network Programming**: TCP sockets
2. **POSIX Threads Tutorial**: Mutexes, condition variables
3. **Trie Data Structure**: Efficient string search
4. **LRU Cache Implementation**: Cache algorithms
5. **CMU DFS Lectures**: Distributed file system concepts

---

## 🚀 Quick Start Commands

```bash
# Setup
cd ~/course-project
mkdir -p src/{common,name_server,storage_server,client}
mkdir -p data/ss{1,2}/{files,metadata/backups}
mkdir -p logs tests

# Build
make clean
make all

# Run
./name_server                # Terminal 1
./storage_server 1           # Terminal 2
./storage_server 2           # Terminal 3
./client                     # Terminal 4 (enter username when prompted)

# Test
make test
```

---

## 📝 Daily Checklist

### Day 1-3: Foundation
- [ ] Directory structure created
- [ ] protocol.h, error_codes.h defined
- [ ] Logger implemented
- [ ] Socket utilities working
- [ ] Makefile compiles all components

### Day 4-6: Name Server
- [ ] NM initialization working
- [ ] SS registration working
- [ ] Client connection working
- [ ] Trie/HashMap implemented
- [ ] LRU cache implemented
- [ ] Basic request routing working

### Day 7-9: Storage Server
- [ ] SS initialization working
- [ ] File I/O working
- [ ] Sentence parser working
- [ ] Sentence locking working
- [ ] Basic WRITE working (no delimiter yet)
- [ ] Undo working
- [ ] Persistence working

### Day 10-11: Client
- [ ] Client connects to NM
- [ ] Command parser working
- [ ] All commands send correct requests
- [ ] Direct SS communication working

### Day 12-14: User Functions
- [ ] VIEW, READ, CREATE, INFO working
- [ ] WRITE fully working (with delimiters)
- [ ] DELETE, UNDO, LIST, STREAM working
- [ ] ACCESS CONTROL, EXEC working

### Day 15-16: System Requirements
- [ ] Full logging implemented
- [ ] Error handling complete
- [ ] Persistence tested
- [ ] Search performance verified

### Day 17-18: Testing
- [ ] Basic tests passing
- [ ] Concurrent tests passing
- [ ] Stress tests passing
- [ ] No memory leaks

### Day 19-20: Polish & Bonus
- [ ] README complete
- [ ] Code commented
- [ ] (Optional) Bonus features
- [ ] Final testing

---

## 🎓 Final Tips

1. **Start with the simple stuff**: CREATE, READ, VIEW before WRITE
2. **Test as you go**: Don't wait until everything is done
3. **Use git**: Commit after each working feature
4. **Debug with printfs**: Then use gdb for harder bugs
5. **Ask for help early**: Don't get stuck for hours
6. **WRITE is hard**: Allocate 2 full days just for this
7. **Focus on core**: Skip bonus if running out of time
8. **Document assumptions**: Can't implement everything perfectly

---

## 🏁 You've Got This!

**Remember**: It's a learning exercise. Aim for a working system with core features first. Polish and bonus features come second.

**Good luck! 🚀**
