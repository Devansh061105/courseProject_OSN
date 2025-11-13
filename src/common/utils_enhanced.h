#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <sys/socket.h>

// Socket utilities
int create_server_socket(int port);
int connect_to_server(const char* ip, int port);
int send_message(int sockfd, const void* data, size_t len);
int recv_message(int sockfd, void* data, size_t len);
int send_all(int sockfd, const void* data, size_t len);
int recv_all(int sockfd, void* data, size_t len);

// Socket configuration
int set_socket_timeout(int sockfd, int seconds);
int set_socket_nonblocking(int sockfd);
int set_socket_blocking(int sockfd);

// Network utilities
int get_peer_info(int sockfd, char* ip, int ip_len, int* port);
int get_local_ip(char* ip, int ip_len);

// String utilities
void trim_whitespace(char* str);
char** split_string(const char* str, const char* delim, int* count);
void free_string_array(char** arr, int count);
bool starts_with(const char* str, const char* prefix);
bool ends_with(const char* str, const char* suffix);

// File utilities
bool file_exists(const char* path);
long get_file_size(const char* path);
bool create_directory_recursive(const char* path);
bool copy_file(const char* src, const char* dst);

// Time utilities
long long current_timestamp_ms();
void format_timestamp(char* buffer, size_t len);

#endif // UTILS_H
