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
#include <chrono> // For timing measurements

#define BUFFER_SIZE 4096
#define LGREEN "\033[1;32m"
#define LBLUE "\033[1;34m"
#define RESET "\033[0m"

using namespace std;
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
            // Remove the EOF marker from the output
            completeResponse = completeResponse.substr(0, completeResponse.find("EOF\n"));
        }
    }

    // Split the response into lines and display
    std::istringstream iss(completeResponse);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) {
            std::cout << line << std::endl;
        }
    }
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

int main(int argc, char *argv[]){
    if(argc != 3){
        cout << "Usage: ./client <server_ip> <port>" << endl;
        return 1;
    }

    char* SERVER_IP = argv[1];
    int PORT = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1){
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    cout << "Connected to FTP server at " << SERVER_IP << ":" << PORT << endl;

    while (true){
        cout <<LGREEN<< "\nftp: "<<LBLUE;
        send_command(sock, "pwd");
        cout<<RESET<<" > ";
        string input;
        getline(cin, input);
        if (input.empty())
            continue;

        string command = input.substr(0, input.find(" "));
        string arg = (input.find(" ") != string::npos) ? input.substr(input.find(" ") + 1) : "";

        if (command == "lls" && arg.empty()){
            DIR *dir;
            struct dirent *entry;
            struct stat file_stat;
            vector<pair<string, bool>> entries;  // pair of entry string and is_directory flag
            size_t max_perms = 0, max_links = 0, max_uid = 0, max_gid = 0, max_size = 0, max_time = 0, max_name = 0;

            if ((dir = opendir(".")) != NULL){
                // First pass: collect all entries and find max lengths
                while ((entry = readdir(dir)) != NULL){
                    string file_name(entry->d_name);
                    string full_path = "./" + file_name;
                    
                    if (stat(full_path.c_str(), &file_stat) == 0) {
                        // Format permissions
                        string permissions = "";
                        permissions += (S_ISDIR(file_stat.st_mode)) ? "d" : "-";
                        permissions += (file_stat.st_mode & S_IRUSR) ? "r" : "-";
                        permissions += (file_stat.st_mode & S_IWUSR) ? "w" : "-";
                        permissions += (file_stat.st_mode & S_IXUSR) ? "x" : "-";
                        permissions += (file_stat.st_mode & S_IRGRP) ? "r" : "-";
                        permissions += (file_stat.st_mode & S_IWGRP) ? "w" : "-";
                        permissions += (file_stat.st_mode & S_IXGRP) ? "x" : "-";
                        permissions += (file_stat.st_mode & S_IROTH) ? "r" : "-";
                        permissions += (file_stat.st_mode & S_IWOTH) ? "w" : "-";
                        permissions += (file_stat.st_mode & S_IXOTH) ? "x" : "-";

                        // Format size
                        string size = to_string(file_stat.st_size);

                        // Format time
                        char time_str[20];
                        strftime(time_str, sizeof(time_str), "%b %d %H:%M", localtime(&file_stat.st_mtime));

                        // Update max lengths
                        max_perms = max(max_perms, permissions.length());
                        max_links = max(max_links, to_string(file_stat.st_nlink).length());
                        max_uid = max(max_uid, to_string(file_stat.st_uid).length());
                        max_gid = max(max_gid, to_string(file_stat.st_gid).length());
                        max_size = max(max_size, size.length());
                        max_time = max(max_time, strlen(time_str));
                        max_name = max(max_name, file_name.length());

                        // Create the detailed listing
                        string entryLine = permissions + " " + 
                                         to_string(file_stat.st_nlink) + " " +
                                         to_string(file_stat.st_uid) + " " +
                                         to_string(file_stat.st_gid) + " " +
                                         size + " " +
                                         time_str + " " +
                                         file_name;
                        
                        entries.push_back({entryLine, S_ISDIR(file_stat.st_mode)});
                    }
                }
                closedir(dir);

                // Sort entries: directories first, then alphabetically
                sort(entries.begin(), entries.end(), [](const pair<string, bool>& a, const pair<string, bool>& b) {
                    if (a.second != b.second) {
                        return a.second > b.second;  // Directories first
                    }
                    // Extract filenames for comparison
                    string a_name = a.first.substr(a.first.rfind(" ") + 1);
                    string b_name = b.first.substr(b.first.rfind(" ") + 1);
                    return a_name < b_name;
                });

                // Second pass: display formatted entries
                for (const auto& entry : entries) {
                    stringstream ss(entry.first);
                    string perms, links, uid, gid, size, time, name;
                    ss >> perms >> links >> uid >> gid >> size >> time;
                    getline(ss, name); // Get the rest as filename (may contain spaces)
                    name = name.substr(1); // Remove leading space

                    // Format with padding
                    cout << perms << string(max_perms - perms.length() + 2, ' ')
                         << links << string(max_links - links.length() + 2, ' ')
                         << uid << string(max_uid - uid.length() + 2, ' ')
                         << gid << string(max_gid - gid.length() + 2, ' ')
                         << size << string(max_size - size.length() + 2, ' ')
                         << time << string(max_time - time.length() + 2, ' ')
                         << name << endl;
                }
            }
            else{
                cout << "Error listing directory\n";
            }
        }

        else if (command == "help"){
            send_command(sock, "help");
        }     

        else if (command == "lcd"){
            if (chdir(arg.c_str()) == 0)
                cout << "Directory changed\n";
            else
                cout << "Error changing directory\n";
        }else if(command == "lpwd"){
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                cout << "Current directory: " << cwd << endl;
            } else {
                perror("getcwd() error");
            }
        }

        else if (command == "lchmod"){
            size_t space = arg.find(" ");
            if (space == string::npos){
                cout << "Usage: lchmod <mode> <file>\n";
                continue;
            }

            string mode = arg.substr(0, space);
            string filename = arg.substr(space + 1);

            if (chmod(filename.c_str(), stoi(mode, nullptr, 8)) == 0)
                cout << "Permissions changed\n";
            else
                cout << "Error changing permissions\n";
        }

        else if (command == "put"){
            if(is_dir(arg)){
                cout << "Error: Cannot upload a directory\n";
                continue;
            }
            handle_put(sock, arg);
        }

        else if (command == "get"){
            handle_get(sock, arg);
        }

        else if (command == "close"){
            send_command(sock, "close");
            break;
        }
        else if(command == "ls" && arg.empty()){
            send_commandls(sock,"ls");
        }

        else{
            send_command(sock, input);
        }
    }

    close(sock);
    return 0;
}

void send_command(int sock, string command){
    char buffer[BUFFER_SIZE];
    send(sock, command.c_str(), command.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    cout << buffer;
}

// Helper function to format file size
string format_size(uint64_t bytes) {
    static const vector<string> units = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double size = bytes;
    
    while (size >= 1024 && unit < units.size() - 1) {
        size /= 1024;
        unit++;
    }
    
    stringstream ss;
    ss << fixed << setprecision(2) << size << " " << units[unit];
    return ss.str();
}

// Helper function to calculate and display transfer statistics
void display_transfer_stats(uint64_t bytes, const chrono::duration<double>& elapsed_time) {
    double seconds = elapsed_time.count();
    double speed = bytes / seconds;
    
    cout << "Time elapsed: " << fixed << setprecision(2) << seconds << " seconds" << endl;
    cout << "Data transferred: " << format_size(bytes) << endl;
    cout << "Transfer speed: " << format_size(static_cast<uint64_t>(speed)) << "/s" << endl;
}

void handle_put(int sock, string filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cout << "File not found: " << filename << endl;
        // send(sock, "ERROR", 5, 0);  // Notify server
        return;
    }
    
    string command = "put " + filename;
    send(sock, command.c_str(), command.size(), 0);
    
    char response[BUFFER_SIZE];
    recv(sock, response, BUFFER_SIZE, 0);
    if (strcmp(response, "ERROR") == 0) {
        cout << "Server error" << endl;
        return;
    }
    
    cout<<"Sending "<<filename<<" ..."<<endl;
    const uint32_t ERROR_SIGNAL = 0xFFFFFFFF;
    char buffer[BUFFER_SIZE];
    int p = 1;
    
    // Get file size
    file.seekg(0, ios::end);
    uint64_t file_size = file.tellg();
    file.seekg(0, ios::beg);
    
    // Start timing
    auto start_time = chrono::high_resolution_clock::now();
    uint64_t bytes_sent = 0;

    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        uint32_t chunk_size = file.gcount();
        uint32_t net_chunk_size = htonl(chunk_size);
        
        if (!send_all(sock, &net_chunk_size, sizeof(net_chunk_size))) {
            cout << "Connection lost" << endl;
            p = 0;
            break;
        }
        
        if (!send_all(sock, buffer, chunk_size)) {
            uint32_t net_err = htonl(ERROR_SIGNAL);
            send_all(sock, &net_err, sizeof(net_err));  // Send error signal
            cout << "Transfer failed" << endl;
            p = 0;
            break;
        }
        
        bytes_sent += chunk_size;
    }
    
    // Send EOF marker
    uint32_t zero = 0;
    zero = htonl(zero);
    send_all(sock, &zero, sizeof(zero));
    
    // End timing and calculate statistics
    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed_time = end_time - start_time;
    
    if(p) {
        cout << "Transfer complete" << endl;
        display_transfer_stats(bytes_sent, elapsed_time);
    }
    
    file.close();
}

// Modified handle_get function
void handle_get(int sock, string filename) {
    string command = "get " + filename;
    send(sock, command.c_str(), command.size(), 0);

    // Read exactly 2 bytes to check for "OK"
    char ok[2];
    if (!recv_all(sock, ok, 2) || strncmp(ok, "OK", 2) != 0) {
        cout << "File not found on server or bad response." << endl;
        return;
    }

    ofstream file(filename, ios::binary);
    if (!file) {
        cout << "Local file error" << endl;
        return;
    }

    cout << "Receiving " << filename << " ...\n";

    const uint32_t ERROR_SIGNAL = 0xFFFFFFFF;
    char buffer[BUFFER_SIZE];
    int status = 1;
    
    // Start timing
    auto start_time = chrono::high_resolution_clock::now();
    uint64_t bytes_received = 0;

    while (true) {
        uint32_t net_chunk_size;
        if (!recv_all(sock, &net_chunk_size, sizeof(net_chunk_size))) {
            cout << "Connection lost\n";
            status = 0;
            break;
        }

        uint32_t chunk_size = ntohl(net_chunk_size);

        if (chunk_size == ERROR_SIGNAL) {
            cout << "Server reported error\n";
            status = 0;
            break;
        }

        if (chunk_size == 0) break; // EOF

        if (chunk_size > BUFFER_SIZE) {
            cout << "Received chunk size too big, possible corruption\n";
            status = 0;
            break;
        }

        if (!recv_all(sock, buffer, chunk_size)) {
            cout << "Incomplete chunk received\n";
            status = 0;
            break;
        }

        file.write(buffer, chunk_size);
        if (!file) {
            cout << "File write error\n";
            status = 0;
            break;
        }
        
        bytes_received += chunk_size;
    }

    file.close();
    
    // End timing and calculate statistics
    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed_time = end_time - start_time;
    
    if (status) {
        cout << "File downloaded successfully\n";
        display_transfer_stats(bytes_received, elapsed_time);
    }
}
