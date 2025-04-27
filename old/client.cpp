#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cstdint> 
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <bits/stdc++.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>

#define BUFFER_SIZE 4096
#define LGREEN "\033[1;32m"
#define LBLUE "\033[1;34m"
#define RED "\033[1;31m"
#define YELLOW "\033[1;33m"
#define CYAN "\033[1;36m"
#define RESET "\033[0m"
#define MAX_RETRIES 5
#define RETRY_DELAY 1000000 // 1 second in microseconds

using namespace std;

// Enhanced ls command handler with proper error handling
void send_commandls(int sock, const std::string &command) {
    char buffer[BUFFER_SIZE];
    send(sock, command.c_str(), command.size(), 0);

    std::string completeResponse;
    int bytes_received = 0;
    bool done = false;

    // Read until we receive our custom "EOF" marker.
    while (!done && (bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        completeResponse += buffer;

        // Check if EOF marker is present
        if (completeResponse.find("EOF\n") != std::string::npos) {
            done = true;
            // Optionally remove the EOF marker from the output:
            completeResponse = completeResponse.substr(0, completeResponse.find("EOF\n"));
        }
    }

    if (bytes_received <= 0) {
        std::cout << RED << "Error receiving directory listing from server" << RESET << std::endl;
        return;
    }

    std::cout << completeResponse;
}

// Function to send a command that expects a larger response
void send_command_with_large_response(int sock, const string &command) {
    char buffer[BUFFER_SIZE];
    send(sock, command.c_str(), command.size(), 0);

    string response;
    int bytes_received = 0;
    
    // Keep reading until we get a complete response
    do {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            response += buffer;
        }
    } while (bytes_received == BUFFER_SIZE - 1);
    
    if (bytes_received <= 0) {
        cout << RED << "Error receiving response from server" << RESET << endl;
        return;
    }
    
    cout << CYAN << response << RESET;
}

void send_command(int sock, string command);
void handle_put(int sock, string filename);
void handle_get(int sock, string filename);

bool is_dir(const string path) {
    struct stat path_stat;
    if (stat(path.c_str(), &path_stat) != 0) {
        return false;
    }
    return S_ISDIR(path_stat.st_mode);
}

bool file_exists(const string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool send_all(int sock, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        int bytes = send(sock, p, len, 0);
        if (bytes <= 0) return false;
        p += bytes;
        len -= bytes;
    }
    return true;
}

bool recv_all(int sock, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    while (len > 0) {
        int bytes = recv(sock, p, len, 0);
        if (bytes <= 0) return false;
        p += bytes;
        len -= bytes;
    }
    return true;
}

// Function to display local directory with ls options
void list_local_directory(const string& args) {
    bool show_hidden = false;
    bool long_format = false;
    string target_dir = "."; // Default to current directory
    
    // Parse arguments
    stringstream ss(args);
    string token;
    vector<string> arg_parts;
    
    while (ss >> token) {
        arg_parts.push_back(token);
    }
    
    // Process options and target directory
    for (const auto& arg : arg_parts) {
        if (arg[0] == '-') {
            // Process flags
            for (size_t i = 1; i < arg.size(); i++) {
                if (arg[i] == 'a') show_hidden = true;
                else if (arg[i] == 'l') long_format = true;
            }
        } else {
            // Treat as target directory
            if (is_dir(arg)) {
                target_dir = arg;
            } else {
                cout << RED << "lls: cannot access '" << arg << "': No such file or directory" << RESET << endl;
                return;
            }
        }
    }
    
    DIR *dir;
    struct dirent *entry;
    vector<string> files;
    
    if ((dir = opendir(target_dir.c_str())) != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            string file_name(entry->d_name);
            
            // Skip hidden files unless -a is specified
            if (!show_hidden && !file_name.empty() && file_name[0] == '.')
                continue;
                
            files.push_back(file_name);
        }
        closedir(dir);
        
        // Sort files
        sort(files.begin(), files.end());
        
        // Display files
        for (const auto& file_name : files) {
            if (long_format) {
                struct stat file_stat;
                string full_path = target_dir + "/" + file_name;
                
                if (stat(full_path.c_str(), &file_stat) == 0) {
                    // Print file mode (like drwxr-xr-x)
                    string perms;
                    perms += (S_ISDIR(file_stat.st_mode)) ? 'd' : '-';
                    perms += (file_stat.st_mode & S_IRUSR) ? 'r' : '-';
                    perms += (file_stat.st_mode & S_IWUSR) ? 'w' : '-';
                    perms += (file_stat.st_mode & S_IXUSR) ? 'x' : '-';
                    perms += (file_stat.st_mode & S_IRGRP) ? 'r' : '-';
                    perms += (file_stat.st_mode & S_IWGRP) ? 'w' : '-';
                    perms += (file_stat.st_mode & S_IXGRP) ? 'x' : '-';
                    perms += (file_stat.st_mode & S_IROTH) ? 'r' : '-';
                    perms += (file_stat.st_mode & S_IWOTH) ? 'w' : '-';
                    perms += (file_stat.st_mode & S_IXOTH) ? 'x' : '-';
                    
                    // Get owner and group
                    struct passwd *pw = getpwuid(file_stat.st_uid);
                    struct group *gr = getgrgid(file_stat.st_gid);
                    string owner = pw ? pw->pw_name : to_string(file_stat.st_uid);
                    string group = gr ? gr->gr_name : to_string(file_stat.st_gid);
                    
                    // Format size
                    stringstream size_str;
                    if (file_stat.st_size < 1024)
                        size_str << file_stat.st_size << " B";
                    else if (file_stat.st_size < 1024 * 1024)
                        size_str << fixed << setprecision(1) << (file_stat.st_size / 1024.0) << " KB";
                    else if (file_stat.st_size < 1024 * 1024 * 1024)
                        size_str << fixed << setprecision(1) << (file_stat.st_size / (1024.0 * 1024.0)) << " MB";
                    else
                        size_str << fixed << setprecision(1) << (file_stat.st_size / (1024.0 * 1024.0 * 1024.0)) << " GB";
                    
                    // Format time
                    char time_buf[30];
                    strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", localtime(&file_stat.st_mtime));
                    
                    // Output in format similar to ls -l
                    cout << perms << " ";
                    cout << setw(3) << right << file_stat.st_nlink << " ";
                    cout << setw(8) << left << owner << " ";
                    cout << setw(8) << left << group << " ";
                    cout << setw(8) << right << size_str.str() << " ";
                    cout << time_buf << " ";
                    cout << file_name << endl;
                }
            } else {
                cout << file_name << endl;
            }
        }
    } else {
        cout << RED << "Error listing directory: " << target_dir << RESET << endl;
    }
}

// Parse command and arguments
void parse_command(const string& input, string& command, string& args) {
    size_t space_pos = input.find(' ');
    if (space_pos != string::npos) {
        command = input.substr(0, space_pos);
        args = input.substr(space_pos + 1);
    } else {
        command = input;
        args = "";
    }
}

// Print help message
void print_help() {
    cout << "\nAvailable client commands:\n";
    cout << "  lls [-l] [-a] [directory] - List files in the local directory\n";
    cout << "    -l: Use long listing format\n";
    cout << "    -a: Show hidden files\n";
    cout << "  lcd <directory> - Change local directory\n";
    cout << "  lchmod <mode> <file> - Change local file permissions\n";
    cout << "  lpwd - Print local working directory\n\n";
    
    cout << "Available server commands:\n";
    cout << "  ls [-l] [-a] [directory] - List files in the server directory\n";
    cout << "  cd <directory> - Change server directory\n";
    cout << "  chmod <mode> <file> - Change file permissions on server\n";
    cout << "  put <file> - Upload a file to server\n";
    cout << "  get <file> - Download a file from server\n";
    cout << "  pwd - Print server working directory\n";
    cout << "  status - Show server lock statistics\n";
    cout << "  help - Show server help message\n";
    cout << "  close - Disconnect from server\n\n";
}

// Main function
int main(int argc, char *argv[]) {
    if(argc != 3) {
        cout << "Usage: ./client <server_ip> <port>" << endl;
        return 1;
    }

    char* SERVER_IP = argv[1];
    int PORT = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    cout << "Connected to FTP server at " << SERVER_IP << ":" << PORT << endl;

    while (true) {
        cout << LGREEN << "\nftp: " << LBLUE;
        send_command(sock, "pwd");
        cout << RESET << " > ";
        string input;
        getline(cin, input);
        if (input.empty())
            continue;

        // Parse command and arguments
        string command, args;
        parse_command(input, command, args);

        if (command == "lls") {
            list_local_directory(args);
        }

        else if (command == "help" || command == "?") {
            print_help();
        }     

        else if (command == "lcd") {
            if (args.empty()) {
                cout << "Usage: lcd <directory>\n";
                continue;
            }
            
            if (chdir(args.c_str()) == 0)
                cout << "Directory changed\n";
            else
                cout << RED << "Error changing directory\n" << RESET;
        }
        
        else if (command == "lpwd") {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                cout << "Current local directory: " << cwd << endl;
            } else {
                perror("getcwd() error");
            }
        }

        else if (command == "lchmod") {
            stringstream ss(args);
            string mode_str, filename;
            ss >> mode_str >> filename;
            
            if (mode_str.empty() || filename.empty()) {
                cout << "Usage: lchmod <mode> <file>\n";
                continue;
            }
            
            if (!file_exists(filename)) {
                cout << RED << "lchmod: cannot access '" << filename << "': No such file or directory" << RESET << endl;
                continue;
            }

            try {
                if (chmod(filename.c_str(), stoi(mode_str, nullptr, 8)) == 0)
                    cout << "Permissions changed\n";
                else
                    cout << RED << "Error changing permissions\n" << RESET;
            } catch (const exception& e) {
                cout << RED << "Invalid mode: " << mode_str << RESET << endl;
            }
        }

        else if (command == "put") {
            if (args.empty()) {
                cout << "Usage: put <filename>\n";
                continue;
            }
            
            // Check if it's a directory at the client-side first
            if (is_dir(args)) {
                cout << RED << "Error: Cannot upload a directory: " << args << RESET << endl;
                continue;
            }
            
            if (!file_exists(args)) {
                cout << RED << "put: cannot access '" << args << "': No such file or directory" << RESET << endl;
                continue;
            }
            
            // Retry mechanism for file upload
            bool success = false;
            int retries = 0;
            
            while (!success && retries < MAX_RETRIES) {
                if (retries > 0) {
                    cout << YELLOW << "Retrying upload (" << retries << "/" << MAX_RETRIES << ")..." << RESET << endl;
                    usleep(RETRY_DELAY);
                }
                
                try {
                    handle_put(sock, args);
                    success = true;
                } catch (const std::exception& e) {
                    string error_msg = e.what();
                    cout << RED << "Upload error: " << error_msg << RESET << endl;
                    
                    // Check if we should retry based on the error type
                    if (error_msg.find("directory") != string::npos) {
                        // Don't retry for directory errors
                        break;
                    }
                    
                    // Check if the error is due to a file lock
                    if (error_msg.find("file is locked") != string::npos || 
                        error_msg.find("busy") != string::npos) {
                        cout << YELLOW << "File is currently locked on the server. Waiting for access..." << RESET << endl;
                        // Use a longer delay for lock contention
                        usleep(RETRY_DELAY * 2);
                    }
                    
                    retries++;
                }
            }
            
            if (!success && retries == MAX_RETRIES) {
                cout << RED << "Upload failed after " << MAX_RETRIES << " attempts." << RESET << endl;
            }
        }

        else if (command == "get") {
            if (args.empty()) {
                cout << "Usage: get <filename>\n";
                continue;
            }
            
            // Check if local path is already a directory
            if (file_exists(args) && is_dir(args)) {
                cout << RED << "Error: Local destination is a directory: " << args << RESET << endl;
                continue;
            }
            
            // Retry mechanism for file download
            bool success = false;
            int retries = 0;
            
            while (!success && retries < MAX_RETRIES) {
                if (retries > 0) {
                    cout << YELLOW << "Retrying download (" << retries << "/" << MAX_RETRIES << ")..." << RESET << endl;
                    usleep(RETRY_DELAY);
                }
                
                try {
                    handle_get(sock, args);
                    success = true;
                } catch (const std::exception& e) {
                    string error_msg = e.what();
                    cout << RED << "Download error: " << error_msg << RESET << endl;
                    
                    // Check if we should retry based on the error type
                    if (error_msg.find("directory") != string::npos) {
                        // Don't retry for directory errors
                        break;
                    }
                    
                    // Check if the error is due to a file lock
                    if (error_msg.find("file is locked") != string::npos || 
                        error_msg.find("busy") != string::npos) {
                        cout << YELLOW << "File is currently locked on the server. Waiting for access..." << RESET << endl;
                        // Use a longer delay for lock contention
                        usleep(RETRY_DELAY * 2);
                    }
                    
                    retries++;
                }
            }
            
            if (!success && retries == MAX_RETRIES) {
                cout << RED << "Download failed after " << MAX_RETRIES << " attempts." << RESET << endl;
            }
        }

        else if (command == "close") {
            send_command(sock, "close");
            break;
        }
        
        else if (command == "ls") {
            send_commandls(sock, input); // Pass the full command including options
        }
        
        else if (command == "status") {
            send_command_with_large_response(sock, "status");
        }

        else {
            // For any other command, just forward to server
            send_command(sock, input);
        }
    }

    close(sock);
    return 0;
}

void send_command(int sock, string command) {
    char buffer[BUFFER_SIZE];
    send(sock, command.c_str(), command.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    cout << buffer;
}

void handle_put(int sock, string filename) {
    // Check if the path is a directory on the client side
    if (is_dir(filename)) {
        cout << RED << "Error: Cannot upload a directory: " << filename << RESET << endl;
        throw std::runtime_error("Cannot upload a directory");
    }
    
    ifstream file(filename, ios::binary);
    if (!file) {
        cout << RED << "File not found: " << filename << RESET << endl;
        throw std::runtime_error("File not found");
    }
    
    string command = "put " + filename;
    send(sock, command.c_str(), command.size(), 0);
    
    char response[BUFFER_SIZE];
    recv(sock, response, BUFFER_SIZE, 0);
    
    if (strcmp(response, "ERROR_DIR") == 0) {
        cout << RED << "Error: The target path on server is a directory" << RESET << endl;
        throw std::runtime_error("Target path on server is a directory");
    } else if (strcmp(response, "ERROR") == 0) {
        cout << RED << "Server error" << RESET << endl;
        throw std::runtime_error("Server rejected the upload - file is locked or in use by another client");
    }
    
    cout << "Sending " << filename << " ..." << endl;
    const uint32_t ERROR_SIGNAL = 0xFFFFFFFF;
    char buffer[BUFFER_SIZE];
    int p = 1;

    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        uint32_t chunk_size = file.gcount();
        uint32_t net_chunk_size = htonl(chunk_size);
        
        if (!send_all(sock, &net_chunk_size, sizeof(net_chunk_size))) {
            cout << RED << "Connection lost" << RESET << endl;
            p = 0;
            throw std::runtime_error("Connection lost during upload");
            break;
        }
        
        if (!send_all(sock, buffer, chunk_size)) {
            uint32_t net_err = htonl(ERROR_SIGNAL);
            send_all(sock, &net_err, sizeof(net_err));  // Send error signal
            cout << RED << "Transfer failed" << RESET << endl;
            p = 0;
            throw std::runtime_error("Data transfer failed");
            break;
        }
    }
    
    // Send EOF marker
    uint32_t zero = 0;
    zero = htonl(zero);
    send_all(sock, &zero, sizeof(zero));
    if(p) cout << LGREEN << "Transfer complete" << RESET << endl;
    file.close();
}

void handle_get(int sock, string filename) {
    string command = "get " + filename;
    send(sock, command.c_str(), command.size(), 0);

    // Read response from server
    char response[BUFFER_SIZE];
    memset(response, 0, sizeof(response));
    if (!recv_all(sock, response, 2)) {
        cout << RED << "Server communication error" << RESET << endl;
        throw std::runtime_error("Server communication error");
    }
    
    if (strncmp(response, "OK", 2) == 0) {
        // Response is OK, continue with download
    } else if (strncmp(response, "DIR", 3) == 0) {
        cout << RED << "Error: Cannot download a directory: " << filename << RESET << endl;
        throw std::runtime_error("Cannot download a directory");
    } else if (strncmp(response, "NO", 2) == 0) {
        string error = "File not found on server or file is locked for writing";
        cout << RED << error << ": " << filename << RESET << endl;
        throw std::runtime_error(error);
    } else {
        cout << RED << "Unknown server response" << RESET << endl;
        throw std::runtime_error("Unknown server response");
    }

    // Check if the local path would be a directory
    if (file_exists(filename) && is_dir(filename)) {
        cout << RED << "Error: Local destination is a directory: " << filename << RESET << endl;
        throw std::runtime_error("Local destination is a directory");
    }

    ofstream file(filename, ios::binary);
    if (!file) {
        cout << RED << "Cannot create local file: " << filename << RESET << endl;
        throw std::runtime_error("Local file creation error");
    }

    cout << "Receiving " << filename << " ..." << endl;

    const uint32_t ERROR_SIGNAL = 0xFFFFFFFF;
    char buffer[BUFFER_SIZE];
    int status = 1;

    while (true) {
        uint32_t net_chunk_size;
        if (!recv_all(sock, &net_chunk_size, sizeof(net_chunk_size))) {
            cout << RED << "Connection lost" << RESET << endl;
            status = 0;
            throw std::runtime_error("Connection lost during download");
            break;
        }

        uint32_t chunk_size = ntohl(net_chunk_size);

        if (chunk_size == ERROR_SIGNAL) {
            cout << RED << "Server reported error" << RESET << endl;
            status = 0;
            throw std::runtime_error("Server reported an error during transfer");
            break;
        }

        if (chunk_size == 0) break; // EOF

        if (chunk_size > BUFFER_SIZE) {
            cout << RED << "Received chunk size too big, possible corruption" << RESET << endl;
            status = 0;
            throw std::runtime_error("Data corruption detected");
            break;
        }

        if (!recv_all(sock, buffer, chunk_size)) {
            cout << RED << "Incomplete chunk received" << RESET << endl;
            status = 0;
            throw std::runtime_error("Incomplete data chunk received");
            break;
        }

        file.write(buffer, chunk_size);
        if (!file) {
            cout << RED << "File write error" << RESET << endl;
            status = 0;
            throw std::runtime_error("Local file write error");
            break;
        }
    }

    file.close();
    if (status) cout << LGREEN << "File downloaded successfully" << RESET << endl;
}
