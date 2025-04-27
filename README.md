# Multi-Client FTP Server and Client

A simple yet powerful FTP server and client implementation that supports multiple concurrent clients with features like file transfer, directory listing, and file permissions management.

## Features

- Multi-client support with thread-safe operations
- File upload and download capabilities
- Directory listing with detailed file information
- File permission management (chmod)
- Local and remote directory navigation
- Memory usage monitoring
- Graceful server shutdown
- Concurrent file access with read-write locks

## Prerequisites

- Linux operating system
- GCC/G++ compiler
- Make utility
- Basic knowledge of terminal commands

## Installation

1. Clone the repository:
```bash
git clone <repository-url>
cd Multi-Client-FTP
```

2. Build the project:
```bash
make
```

This will create two executables:
- `server`: The FTP server program
- `client`: The FTP client program

## Usage

### Starting the Server

1. Run the server with a port number (must be between 1025 and 65535):
```bash
./server <port>
```

The server will display its IP address and start listening for connections.

### Using the Client

1. Connect to the server:
```bash
./client <server_ip> <port>
```

### Available Commands

#### Server Commands:
- `ls` - List files in current directory
- `cd <directory>` - Change directory
- `chmod <mode> <file>` - Change file permissions
- `pwd` - Print working directory
- `help` - Show help message
- `close` - Disconnect from server

#### Client Commands:
- `lls` - List files in local directory
- `lcd <directory>` - Change local directory
- `lchmod <mode> <file>` - Change local file permissions
- `lpwd` - Print local working directory
- `put <file>` - Upload a file to server
- `get <file>` - Download a file from server

## Cleaning Up

To remove the compiled executables:
```bash
make clean
```

## Troubleshooting

1. If you get a "port already in use" error:
   - Try using a different port number
   - Make sure no other instance of the server is running

2. If connection fails:
   - Verify the server IP address and port number
   - Check if the server is running
   - Ensure your firewall allows the connection

3. If file operations fail:
   - Check file permissions
   - Ensure you have read/write access to the directories

## Contributing

Feel free to submit issues and enhancement requests!
