CC = g++

# Source files
SERVER_SRC = server.cpp
CLIENT_SRC = client.cpp

# Executable names
SERVER_EXE = server
CLIENT_EXE = client

# Default target
all: $(SERVER_EXE) $(CLIENT_EXE)

# Build server
$(SERVER_EXE): $(SERVER_SRC)
	$(CC) $(SERVER_SRC) -o $(SERVER_EXE)

# Build client
$(CLIENT_EXE): $(CLIENT_SRC)
	$(CC) $(CLIENT_SRC) -o $(CLIENT_EXE)

# Clean build files
clean:
	rm -f $(SERVER_EXE) $(CLIENT_EXE)

# Phony targets
.PHONY: all clean 