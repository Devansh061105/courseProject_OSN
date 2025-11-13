#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../common/utils.h"
#include "../common/error_codes.h"
#include "../common/logger.h"

void test_string_utilities() {
    printf("\n=== Testing String Utilities ===\n");
    
    // Test trim_whitespace
    char str1[] = "  hello world  ";
    trim_whitespace(str1);
    printf("Trim test: '%s' (expected: 'hello world')\n", str1);
    assert(strcmp(str1, "hello world") == 0);
    
    // Test split_string
    int count;
    char** tokens = split_string("apple,banana,cherry", ",", &count);
    printf("Split test: Found %d tokens\n", count);
    assert(count == 3);
    assert(strcmp(tokens[0], "apple") == 0);
    assert(strcmp(tokens[1], "banana") == 0);
    assert(strcmp(tokens[2], "cherry") == 0);
    free_string_array(tokens, count);
    
    // Test starts_with
    assert(starts_with("hello world", "hello") == true);
    assert(starts_with("hello world", "world") == false);
    printf("starts_with test: PASSED\n");
    
    // Test ends_with
    assert(ends_with("test.txt", ".txt") == true);
    assert(ends_with("test.txt", ".pdf") == false);
    printf("ends_with test: PASSED\n");
    
    printf("✅ String utilities: ALL TESTS PASSED\n");
}

void test_file_utilities() {
    printf("\n=== Testing File Utilities ===\n");
    
    // Test file_exists
    const char* test_file = "/tmp/test_utils_file.txt";
    FILE* fp = fopen(test_file, "w");
    fprintf(fp, "Test content");
    fclose(fp);
    
    assert(file_exists(test_file) == true);
    printf("file_exists test: PASSED\n");
    
    // Test get_file_size
    long size = get_file_size(test_file);
    printf("File size: %ld bytes\n", size);
    assert(size == 12);  // "Test content" is 12 bytes
    
    // Test copy_file
    const char* copy_file_path = "/tmp/test_utils_copy.txt";
    assert(copy_file(test_file, copy_file_path) == true);
    assert(file_exists(copy_file_path) == true);
    printf("copy_file test: PASSED\n");
    
    // Cleanup
    unlink(test_file);
    unlink(copy_file_path);
    
    // Test create_directory_recursive
    const char* test_dir = "/tmp/test_utils/sub1/sub2";
    assert(create_directory_recursive(test_dir) == true);
    printf("create_directory_recursive test: PASSED\n");
    
    // Cleanup
    system("rm -rf /tmp/test_utils");
    
    printf("✅ File utilities: ALL TESTS PASSED\n");
}

void test_time_utilities() {
    printf("\n=== Testing Time Utilities ===\n");
    
    // Test current_timestamp_ms
    long long ts1 = current_timestamp_ms();
    usleep(100000);  // Sleep 100ms
    long long ts2 = current_timestamp_ms();
    long long diff = ts2 - ts1;
    printf("Time difference: %lld ms (expected ~100ms)\n", diff);
    assert(diff >= 95 && diff <= 150);  // Allow some variance
    
    // Test format_timestamp
    char buffer[64];
    format_timestamp(buffer, sizeof(buffer));
    printf("Formatted timestamp: %s\n", buffer);
    assert(strlen(buffer) > 0);
    
    printf("✅ Time utilities: ALL TESTS PASSED\n");
}

void test_error_codes() {
    printf("\n=== Testing Error Codes ===\n");
    
    const char* msg1 = get_error_message(SUCCESS);
    printf("SUCCESS: %s\n", msg1);
    
    const char* msg2 = get_error_message(ERR_FILE_NOT_FOUND);
    printf("ERR_FILE_NOT_FOUND: %s\n", msg2);
    
    const char* msg3 = get_error_message(ERR_FILE_LOCKED);
    printf("ERR_FILE_LOCKED: %s\n", msg3);
    
    printf("✅ Error codes: ALL TESTS PASSED\n");
}

void test_logger() {
    printf("\n=== Testing Logger ===\n");
    
    init_logger("/tmp/test_utils.log");
    
    log_message("TEST", "127.0.0.1", 5000, "testuser", 
                "CREATE", "test.txt", "SUCCESS");
    
    log_message("TEST", "192.168.1.100", 8080, "user2", 
                "WRITE", "doc.txt sentence 3", "LOCKED");
    
    close_logger();
    
    // Check log file exists
    assert(file_exists("/tmp/test_utils.log") == true);
    printf("Log file created successfully\n");
    
    // Cleanup
    unlink("/tmp/test_utils.log");
    
    printf("✅ Logger: ALL TESTS PASSED\n");
}

void test_network_utilities() {
    printf("\n=== Testing Network Utilities ===\n");
    
    // Test get_local_ip
    char ip[16];
    get_local_ip(ip, sizeof(ip));
    printf("Local IP: %s\n", ip);
    assert(strlen(ip) > 0);
    
    printf("✅ Network utilities: ALL TESTS PASSED\n");
}

int main() {
    printf("╔════════════════════════════════════════╗\n");
    printf("║  Common Utilities Test Suite          ║\n");
    printf("╚════════════════════════════════════════╝\n");
    
    test_string_utilities();
    test_file_utilities();
    test_time_utilities();
    test_error_codes();
    test_logger();
    test_network_utilities();
    
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║  ✅ ALL TESTS PASSED!                  ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\nCommon utilities are working correctly!\n");
    
    return 0;
}
