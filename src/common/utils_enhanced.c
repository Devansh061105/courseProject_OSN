#include "utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>

// ======================== Socket Utilities ========================

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

    printf("Server socket created on port %d\n", port);
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

// Send all data (handles partial sends)
int send_all(int sockfd, const void* data, size_t len) {
    size_t total_sent = 0;
    const char* ptr = (const char*)data;
    
    while (total_sent < len) {
        ssize_t sent = send(sockfd, ptr + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;  // Interrupted, retry
            perror("send_all failed");
            return -1;
        }
        if (sent == 0) {
            fprintf(stderr, "Connection closed by peer\n");
            return -1;
        }
        total_sent += sent;
    }
    
    return total_sent;
}

// Receive exact amount of data
int recv_all(int sockfd, void* data, size_t len) {
    size_t total_received = 0;
    char* ptr = (char*)data;
    
    while (total_received < len) {
        ssize_t received = recv(sockfd, ptr + total_received, len - total_received, 0);
        if (received < 0) {
            if (errno == EINTR) continue;  // Interrupted, retry
            perror("recv_all failed");
            return -1;
        }
        if (received == 0) {
            fprintf(stderr, "Connection closed by peer\n");
            return -1;
        }
        total_received += received;
    }
    
    return total_received;
}

// ======================== Socket Configuration ========================

int set_socket_timeout(int sockfd, int seconds) {
    struct timeval timeout;
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
        return -1;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_SNDTIMEO failed");
        return -1;
    }
    
    return 0;
}

int set_socket_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl F_GETFL failed");
        return -1;
    }
    
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl F_SETFL failed");
        return -1;
    }
    
    return 0;
}

int set_socket_blocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl F_GETFL failed");
        return -1;
    }
    
    if (fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        perror("fcntl F_SETFL failed");
        return -1;
    }
    
    return 0;
}

// ======================== Network Utilities ========================

int get_peer_info(int sockfd, char* ip, int ip_len, int* port) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getpeername(sockfd, (struct sockaddr*)&addr, &addr_len) < 0) {
        perror("getpeername failed");
        return -1;
    }
    
    if (inet_ntop(AF_INET, &addr.sin_addr, ip, ip_len) == NULL) {
        perror("inet_ntop failed");
        return -1;
    }
    
    if (port != NULL) {
        *port = ntohs(addr.sin_port);
    }
    
    return 0;
}

int get_local_ip(char* ip, int ip_len) {
    // Simple implementation: try to connect to external IP to get local IP
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        strncpy(ip, "127.0.0.1", ip_len);
        return 0;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);  // DNS port
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);
    
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        strncpy(ip, "127.0.0.1", ip_len);
        return 0;
    }
    
    struct sockaddr_in local_addr;
    socklen_t local_addr_len = sizeof(local_addr);
    if (getsockname(sockfd, (struct sockaddr*)&local_addr, &local_addr_len) < 0) {
        close(sockfd);
        strncpy(ip, "127.0.0.1", ip_len);
        return 0;
    }
    
    inet_ntop(AF_INET, &local_addr.sin_addr, ip, ip_len);
    close(sockfd);
    
    return 0;
}

// ======================== String Utilities ========================

void trim_whitespace(char* str) {
    if (str == NULL) return;
    
    // Trim leading whitespace
    char* start = str;
    while (isspace(*start)) start++;
    
    // Trim trailing whitespace
    char* end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    
    // Copy trimmed string
    size_t len = end - start + 1;
    memmove(str, start, len);
    str[len] = '\0';
}

char** split_string(const char* str, const char* delim, int* count) {
    if (str == NULL || delim == NULL || count == NULL) {
        if (count) *count = 0;
        return NULL;
    }
    
    // Count tokens
    char* str_copy = strdup(str);
    char* token;
    int token_count = 0;
    
    token = strtok(str_copy, delim);
    while (token != NULL) {
        token_count++;
        token = strtok(NULL, delim);
    }
    free(str_copy);
    
    if (token_count == 0) {
        *count = 0;
        return NULL;
    }
    
    // Allocate array
    char** tokens = (char**)malloc(sizeof(char*) * token_count);
    if (tokens == NULL) {
        *count = 0;
        return NULL;
    }
    
    // Split string
    str_copy = strdup(str);
    int i = 0;
    token = strtok(str_copy, delim);
    while (token != NULL) {
        tokens[i] = strdup(token);
        i++;
        token = strtok(NULL, delim);
    }
    free(str_copy);
    
    *count = token_count;
    return tokens;
}

void free_string_array(char** arr, int count) {
    if (arr == NULL) return;
    
    for (int i = 0; i < count; i++) {
        if (arr[i] != NULL) {
            free(arr[i]);
        }
    }
    free(arr);
}

bool starts_with(const char* str, const char* prefix) {
    if (str == NULL || prefix == NULL) return false;
    size_t prefix_len = strlen(prefix);
    size_t str_len = strlen(str);
    if (prefix_len > str_len) return false;
    return strncmp(str, prefix, prefix_len) == 0;
}

bool ends_with(const char* str, const char* suffix) {
    if (str == NULL || suffix == NULL) return false;
    size_t suffix_len = strlen(suffix);
    size_t str_len = strlen(str);
    if (suffix_len > str_len) return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

// ======================== File Utilities ========================

bool file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

long get_file_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return st.st_size;
}

bool create_directory_recursive(const char* path) {
    if (path == NULL || strlen(path) == 0) return false;
    
    char* path_copy = strdup(path);
    char* p = path_copy;
    
    // Skip leading slashes
    while (*p == '/') p++;
    
    while (*p) {
        while (*p && *p != '/') p++;
        
        char old_char = *p;
        *p = '\0';
        
        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            free(path_copy);
            return false;
        }
        
        *p = old_char;
        if (*p) p++;
    }
    
    free(path_copy);
    return true;
}

bool copy_file(const char* src, const char* dst) {
    FILE* src_file = fopen(src, "rb");
    if (src_file == NULL) {
        perror("Failed to open source file");
        return false;
    }
    
    FILE* dst_file = fopen(dst, "wb");
    if (dst_file == NULL) {
        perror("Failed to open destination file");
        fclose(src_file);
        return false;
    }
    
    char buffer[4096];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
        if (fwrite(buffer, 1, bytes, dst_file) != bytes) {
            perror("Failed to write to destination file");
            fclose(src_file);
            fclose(dst_file);
            return false;
        }
    }
    
    fclose(src_file);
    fclose(dst_file);
    return true;
}

// ======================== Time Utilities ========================

long long current_timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

void format_timestamp(char* buffer, size_t len) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buffer, len, "%Y-%m-%d %H:%M:%S", tm_info);
}
