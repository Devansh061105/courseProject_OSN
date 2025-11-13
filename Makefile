CC = gcc
CFLAGS = -Wall -Wextra -pthread -g -O2
LDFLAGS = -lpthread

# Source files
COMMON_SRCS = src/common/error_codes.c src/common/logger.c src/common/utils.c
NM_SRCS = src/name_server/main.c src/name_server/nm_server.c
SS_SRCS = src/storage_server/main.c src/storage_server/ss_server.c
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
	rm -f src/name_server/*.o src/storage_server/*.o src/client/*.o
	rm -f logs/*.log
	rm -f data/*/files/* data/*/metadata/*.meta data/*/metadata/backups/*

test: all
	@echo "Running basic tests..."
	bash tests/basic_test.sh

.PHONY: all clean test
