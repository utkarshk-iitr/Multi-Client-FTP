CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2
LDFLAGS = -pthread

# Debug flags (for debug target)
DEBUG_FLAGS = -g -O0 -DDEBUG

# Binary names
SERVER_BIN = server
CLIENT_BIN = client
FTPSERVER_BIN = ftpserver

# Source files
SERVER_SRC = se.cpp
CLIENT_SRC = cl.cpp

# Targets
all: $(SERVER_BIN) $(CLIENT_BIN)

# Build rules
$(SERVER_BIN): $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# Target for the ftpserver (uncomment if source file is available)
# $(FTPSERVER_BIN): ftpserver.cpp
#	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# Debug builds
debug: debug_server debug_client

debug_server: $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $< -o $(SERVER_BIN)_debug $(LDFLAGS)

debug_client: $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $< -o $(CLIENT_BIN)_debug $(LDFLAGS)

# Install target (adjust destination as needed)
install: all
	install -m 755 $(SERVER_BIN) /usr/local/bin/
	install -m 755 $(CLIENT_BIN) /usr/local/bin/

# Clean targets
clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(FTPSERVER_BIN)

cleanall: clean
	rm -f $(SERVER_BIN)_debug $(CLIENT_BIN)_debug
	rm -f *.o core

# Help target
help:
	@echo "Available targets:"
	@echo "  all       : Build server and client (default)"
	@echo "  debug     : Build debug versions with symbols"
	@echo "  clean     : Remove binaries"
	@echo "  cleanall  : Remove all generated files"
	@echo "  install   : Install binaries to /usr/local/bin"
	@echo "  help      : Show this help message"

.PHONY: all clean cleanall debug debug_server debug_client install help 