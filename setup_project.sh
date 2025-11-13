#!/bin/bash

# Docs++ Project Setup Script
# Run this in WSL to create the complete project structure

echo "=== Docs++ Project Setup ==="
echo "Setting up project structure..."

# Create main directories
mkdir -p src/common
mkdir -p src/name_server
mkdir -p src/storage_server
mkdir -p src/client

# Create data directories for storage servers
mkdir -p data/ss1/files
mkdir -p data/ss1/metadata/backups
mkdir -p data/ss2/files
mkdir -p data/ss2/metadata/backups

# Create logs directory
mkdir -p logs

# Create tests directory
mkdir -p tests

echo "Directory structure created!"

# Create placeholder files for common module
echo "Creating common module templates..."

cat > src/common/protocol.h << 'EOF'
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

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
    int status_code;
    char message[MAX_MESSAGE];
    char ss_ip[16];
    int ss_port;
} Response;

#endif // PROTOCOL_H
EOF

cat > src/common/error_codes.h << 'EOF'
#ifndef ERROR_CODES_H
#define ERROR_CODES_H

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

const char* get_error_message(int error_code);

#endif // ERROR_CODES_H
EOF

cat > src/common/error_codes.c << 'EOF'
#include "error_codes.h"

const char* get_error_message(int error_code) {
    switch(error_code) {
        case SUCCESS:
            return "Operation successful.";
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
        case ERR_INVALID_COMMAND:
            return "Invalid command.";
        case ERR_FILE_ALREADY_EXISTS:
            return "File already exists.";
        case ERR_PERMISSION_DENIED:
            return "Permission denied.";
        case ERR_INVALID_USERNAME:
            return "Invalid username.";
        case ERR_CONNECTION_FAILED:
            return "Connection failed.";
        case ERR_NETWORK_ERROR:
            return "Network error occurred.";
        default:
            return "Unknown error occurred.";
    }
}
EOF

cat > src/common/logger.h << 'EOF'
#ifndef LOGGER_H
#define LOGGER_H

void log_message(const char* component, const char* ip, int port,
                 const char* username, const char* operation,
                 const char* details, const char* result);

void init_logger(const char* log_file_path);
void close_logger();

#endif // LOGGER_H
EOF

cat > src/common/logger.c << 'EOF'
#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

static FILE* log_file = NULL;

void init_logger(const char* log_file_path) {
    log_file = fopen(log_file_path, "a");
    if (!log_file) {
        fprintf(stderr, "Failed to open log file: %s\n", log_file_path);
    }
}

void close_logger() {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

void log_message(const char* component, const char* ip, int port,
                 const char* username, const char* operation,
                 const char* details, const char* result) {
    char timestamp[64];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
             localtime(&now));

    // Log to file
    if (log_file) {
        fprintf(log_file, "[%s] [%s] [%s:%d] [%s] %s - %s - %s\n",
                timestamp, component, ip, port, username,
                operation, details, result);
        fflush(log_file);
    }

    // Also print to terminal
    printf("[%s] [%s] [%s:%d] [%s] %s - %s\n",
           timestamp, component, ip, port, username, operation, result);
}
EOF

cat > src/common/utils.h << 'EOF'
#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

// Socket utilities
int create_server_socket(int port);
int connect_to_server(const char* ip, int port);
int send_message(int sockfd, const void* data, size_t len);
int recv_message(int sockfd, void* data, size_t len);

#endif // UTILS_H
EOF

cat > src/common/utils.c << 'EOF'
#include "utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(sockfd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 10) < 0) {
        perror("listen failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int connect_to_server(const char* ip, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("invalid address");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connection failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int send_message(int sockfd, const void* data, size_t len) {
    ssize_t sent = send(sockfd, data, len, 0);
    if (sent < 0) {
        perror("send failed");
        return -1;
    }
    return sent;
}

int recv_message(int sockfd, void* data, size_t len) {
    ssize_t received = recv(sockfd, data, len, 0);
    if (received < 0) {
        perror("recv failed");
        return -1;
    }
    return received;
}
EOF

# Create placeholder main files
cat > src/name_server/main.c << 'EOF'
#include <stdio.h>
#include "../common/protocol.h"
#include "../common/logger.h"
#include "../common/utils.h"

int main(int argc, char* argv[]) {
    printf("Name Server starting...\n");
    init_logger("logs/name_server.log");
    
    // TODO: Implement name server logic
    
    close_logger();
    return 0;
}
EOF

cat > src/storage_server/main.c << 'EOF'
#include <stdio.h>
#include "../common/protocol.h"
#include "../common/logger.h"
#include "../common/utils.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_id>\n", argv[0]);
        return 1;
    }
    
    int server_id = atoi(argv[1]);
    printf("Storage Server %d starting...\n", server_id);
    
    char log_path[256];
    sprintf(log_path, "logs/ss%d.log", server_id);
    init_logger(log_path);
    
    // TODO: Implement storage server logic
    
    close_logger();
    return 0;
}
EOF

cat > src/client/main.c << 'EOF'
#include <stdio.h>
#include <string.h>
#include "../common/protocol.h"
#include "../common/utils.h"

int main(int argc, char* argv[]) {
    char username[MAX_USERNAME];
    
    printf("Enter username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;  // Remove newline
    
    printf("Client started for user: %s\n", username);
    
    // TODO: Implement client logic
    
    return 0;
}
EOF

# Create Makefile
cat > Makefile << 'EOF'
CC = gcc
CFLAGS = -Wall -Wextra -pthread -g -O2
LDFLAGS = -lpthread

# Source files
COMMON_SRCS = src/common/protocol.c src/common/error_codes.c src/common/logger.c src/common/utils.c
NM_SRCS = src/name_server/main.c
SS_SRCS = src/storage_server/main.c
CLIENT_SRCS = src/client/main.c

# Object files
COMMON_OBJS = $(COMMON_SRCS:.c=.o)
NM_OBJS = $(NM_SRCS:.c=.o)
SS_OBJS = $(SS_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# Targets
all: name_server storage_server client

name_server: $(COMMON_OBJS) $(NM_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

storage_server: $(COMMON_OBJS) $(SS_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: $(COMMON_OBJS) $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f name_server storage_server client
	rm -f $(COMMON_OBJS) $(NM_OBJS) $(SS_OBJS) $(CLIENT_OBJS)
	rm -f logs/*.log
	rm -f data/*/files/* data/*/metadata/*.meta data/*/metadata/backups/*

test: all
	@echo "Running basic tests..."
	bash tests/basic_test.sh

.PHONY: all clean test
EOF

# Create basic README
cat > README.md << 'EOF'
# Docs++ Distributed File System

A distributed document collaboration system built for the OSN course project.

## Building

```bash
make clean
make all
```

## Running

Start the Name Server:
```bash
./name_server
```

Start Storage Servers (in separate terminals):
```bash
./storage_server 1
./storage_server 2
```

Start Client:
```bash
./client
```

## Project Structure

- `src/common/` - Shared code (protocol, logging, utilities)
- `src/name_server/` - Name server implementation
- `src/storage_server/` - Storage server implementation
- `src/client/` - Client implementation
- `data/` - Storage server data files
- `logs/` - Log files
- `tests/` - Test scripts

## Features

See `IMPLEMENTATION_PLAN.md` for detailed implementation guide.

## Testing

```bash
make test
```

## Team

- [Add team member names here]
EOF

echo ""
echo "✅ Project structure created successfully!"
echo ""
echo "Next steps:"
echo "1. Review the IMPLEMENTATION_PLAN.md for detailed implementation guide"
echo "2. Start implementing Phase 1 (Common utilities)"
echo "3. Build incrementally: make && ./name_server"
echo "4. Test frequently!"
echo ""
echo "Quick start:"
echo "  cd ~/course-project"
echo "  make"
echo "  ./name_server    # Terminal 1"
echo "  ./storage_server 1  # Terminal 2"
echo "  ./client         # Terminal 3"
echo ""
