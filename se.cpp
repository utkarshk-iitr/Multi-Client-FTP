#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <bits/stdc++.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <limits.h>  // For PATH_MAX
#include <stdlib.h>  // For realpath

// #define port 8080
#define MAX_CLIENTS 8
#define BUFFER_SIZE 4096
#define LGREEN "\033[1;32m"
#define DGREEN "\033[0;32m"
#define LBLUE "\033[1;34m"
#define RED "\033[1;31m"
#define YELLOW "\033[1;33m"
#define CYAN "\033[1;36m"
#define RESET "\033[0m"
int client_count = 0;

using namespace std;

// File access synchronization - Reader-Writer lock implementation
struct rwlock_t {
    pthread_mutex_t mutex;          // Basic lock for the structure
    pthread_cond_t readers_cv;      // For readers to wait
    pthread_cond_t writers_cv;      // For writers to wait
    int readers_count;              // Number of active readers
    int writers_count;              // Number of active writers
    int waiting_writers;            // Number of waiting writers
    bool writer_active;             // Is there an active writer?
    unordered_map<string, bool> locked_files; // Track which files are currently being written to
    unordered_map<string, int> file_readers;  // Track number of readers per file
};

rwlock_t rwlock;


// Initialize the reader-writer lock
void rwlock_init(rwlock_t *rwlock) {
    pthread_mutex_init(&rwlock->mutex, NULL);
    pthread_cond_init(&rwlock->readers_cv, NULL);
    pthread_cond_init(&rwlock->writers_cv, NULL);
    rwlock->readers_count = 0;
    rwlock->writers_count = 0;
    rwlock->waiting_writers = 0;
    rwlock->writer_active = false;
}

// Add this function before main()
string clean_path(const string& path) {
    char resolved_path[PATH_MAX];
    if (realpath(path.c_str(), resolved_path) != NULL) {
        return string(resolved_path);
    }
    return path;  // Return original path if realpath fails
}

// Acquire a read lock on a specific file
void read_lock(rwlock_t *rwlock, const string &filename) {
    pthread_mutex_lock(&rwlock->mutex);
    
    string clean_filename = clean_path(filename);
    
    // Display waiting status if needed
    if (rwlock->locked_files[clean_filename] || rwlock->waiting_writers > 0) {
        cout << YELLOW << "Waiting for READ lock on: " << clean_filename;
        
        if (rwlock->locked_files[clean_filename])
            cout << " (file is being written)";
        if (rwlock->waiting_writers > 0)
            cout << " (writers are waiting: " << rwlock->waiting_writers << ")";
        
        cout << RESET << endl;
    }
    
    // Wait if there's an active writer or waiting writers for this file
    while (rwlock->locked_files[clean_filename] || rwlock->waiting_writers > 0) {
        pthread_cond_wait(&rwlock->readers_cv, &rwlock->mutex);
    }
    
    // Increment readers count
    rwlock->readers_count++;
    rwlock->file_readers[clean_filename]++;
    
    cout << CYAN <<"Acquired READ lock on: " << clean_filename 
         << " (readers: " << rwlock->file_readers[clean_filename] << ")" << RESET << endl;
    
    pthread_mutex_unlock(&rwlock->mutex);
}

// Release a read lock
void read_unlock(rwlock_t *rwlock, const string &filename) {
    pthread_mutex_lock(&rwlock->mutex);
    
    string clean_filename = clean_path(filename);
    
    // Decrement readers count
    rwlock->readers_count--;
    rwlock->file_readers[clean_filename]--;
    
    cout << CYAN << "Released READ lock on: " << clean_filename;
    
    if (rwlock->file_readers[clean_filename] > 0) {
        cout << " (readers remaining: " << rwlock->file_readers[clean_filename] << ")";
    } else {
        cout << " (no readers left)";
    }
    cout << RESET << endl;
    
    // If no more readers, signal any waiting writers
    if (rwlock->readers_count == 0 && rwlock->waiting_writers > 0) {
        cout << CYAN << "] Signaling waiting writers" << RESET << endl;
        pthread_cond_signal(&rwlock->writers_cv);
    }
    
    pthread_mutex_unlock(&rwlock->mutex);
}

// Acquire a write lock on a specific file
void write_lock(rwlock_t *rwlock, const string &filename) {
    pthread_mutex_lock(&rwlock->mutex);
    
    string clean_filename = clean_path(filename);
    
    // Increment waiting writers
    rwlock->waiting_writers++;
    
    // Display waiting status if needed
    if (rwlock->readers_count > 0 || rwlock->locked_files[clean_filename]) {
        cout << YELLOW << "Waiting for WRITE lock on: " << clean_filename;
        
        if (rwlock->readers_count > 0)
            cout << " (active readers: " << rwlock->file_readers[clean_filename] << ")";
        if (rwlock->locked_files[clean_filename])
            cout << " (file is locked for writing)";
        
        cout << RESET << endl;
    }
    
    // Wait if there are active readers or another writer on this file
    while (rwlock->readers_count > 0 || rwlock->locked_files[clean_filename]) {
        pthread_cond_wait(&rwlock->writers_cv, &rwlock->mutex);
    }
    
    // Decrement waiting writers and mark as active writer
    rwlock->waiting_writers--;
    rwlock->writers_count++;
    rwlock->locked_files[clean_filename] = true;
    
    cout << DGREEN << "Acquired WRITE lock on: " << clean_filename 
         << " (waiting writers: " << rwlock->waiting_writers << ")" << RESET << endl;
    
    pthread_mutex_unlock(&rwlock->mutex);
}

// Release a write lock
void write_unlock(rwlock_t *rwlock, const string &filename) {
    pthread_mutex_lock(&rwlock->mutex);
    
    string clean_filename = clean_path(filename);
    
    // Decrement writers count and mark file as unlocked
    rwlock->writers_count--;
    rwlock->locked_files[clean_filename] = false;
    
    cout << DGREEN << "Released WRITE lock on: " << clean_filename << RESET << endl;
    
    // Wake up all waiting threads
    // Readers if no waiting writers, otherwise wake a writer
    if (rwlock->waiting_writers > 0) {
        cout << DGREEN << "Signaling next writer" << RESET << endl;
        pthread_cond_signal(&rwlock->writers_cv);
    } else {
        cout << DGREEN << "Broadcasting to all readers" << RESET << endl;
        pthread_cond_broadcast(&rwlock->readers_cv);
    }
    
    pthread_mutex_unlock(&rwlock->mutex);
}

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
void *handle_client(void *client_socket);

bool is_dir(const string path) {
    struct stat path_stat;
    if (stat(path.c_str(), &path_stat) != 0) {
        return false;
    }
    return S_ISDIR(path_stat.st_mode);
}

string getip(){
    struct ifaddrs *interfaces = nullptr;
    struct ifaddrs *ifa = nullptr;
    if (getifaddrs(&interfaces) == -1) {
        return "-1";
    }
    
    string myip;
    for (ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            void* addr_ptr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, addr_ptr, ip, sizeof(ip));
            if (string(ip) != "127.0.0.1") myip = ip;
        }
    }
    freeifaddrs(interfaces);
    return myip;
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

int main(int argc, char* argv[]){
    if (argc != 2) {
        cerr << "Usage: ./server <port>" <<endl;
        return 1;
    }
    int port = atoi(argv[1]);

    if (port <= 1024 || port > 65535) { 
        cerr << "Please provide a valid port number in the range 1025-65535.\n";
        return 1;
    }

    // Initialize the reader-writer lock
    rwlock_init(&rwlock);

    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1){
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("Reuse failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0){
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    string myip = getip();
    if(myip=="-1"){
        cout<<"Error in getting IP"<<endl;
        return 0;
    }
    cout << "FTP Server started at " << myip<<":"<<port << " ...\n"<<endl;

    while (true){
        client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0){
            perror("Client accept failed");
            continue;
        }

        pthread_t thread_id;
        client_count++;
        pthread_create(&thread_id, NULL, handle_client, (void *)&client_socket);
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}

void *handle_client(void *client_socket){
    int sock = *(int *)client_socket;
    char buffer[BUFFER_SIZE];
    string client_directory = ".";
    cout<<LGREEN<<"Clients connected: "<<client_count<<RESET<<endl<<endl;

    while (true){
        memset(buffer, 0, BUFFER_SIZE);
        if (recv(sock, buffer, BUFFER_SIZE, 0) <= 0){
            cout << LBLUE<<"Client disconnected.\n";
            client_count--;
            cout<<"Clients remaining: "<<client_count<<RESET<<endl<<endl;
            close(sock);
            pthread_exit(NULL);
        }

        string command(buffer);
        command = command.substr(0, command.find("\n"));

            if (command == "ls"){
                DIR *dir;
                struct dirent *entry;
                struct stat file_stat;
                vector<pair<string, bool>> entries;  // pair of entry string and is_directory flag
                size_t max_perms = 0, max_links = 0, max_uid = 0, max_gid = 0, max_size = 0, max_time = 0, max_name = 0;
            
                if ((dir = opendir(client_directory.c_str())) != NULL) {
                    // First pass: collect all entries and find max lengths
                    while ((entry = readdir(dir)) != NULL) {
                        string file_name(entry->d_name);
                        string full_path = client_directory + "/" + file_name;
                        
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

                    // Second pass: send formatted entries
                    for (const auto& entry : entries) {
                        stringstream ss(entry.first);
                        string perms, links, uid, gid, size, time, name;
                        ss >> perms >> links >> uid >> gid >> size >> time;
                        getline(ss, name); // Get the rest as filename (may contain spaces)
                        name = name.substr(1); // Remove leading space

                        // Format with padding
                        string formatted = perms + string(max_perms - perms.length() + 2, ' ') +
                                         links + string(max_links - links.length() + 2, ' ') +
                                         uid + string(max_uid - uid.length() + 2, ' ') +
                                         gid + string(max_gid - gid.length() + 2, ' ') +
                                         size + string(max_size - size.length() + 2, ' ') +
                                         time + string(max_time - time.length() + 2, ' ') +
                                         name + "\n";
                        
                        send(sock, formatted.c_str(), formatted.size(), 0);
                    }
                } else {
                    send(sock, "Error opening directory\n", 23, 0);
                }
                // Send end-of-transmission marker
                string eofMarker = "EOF\n";
                send(sock, eofMarker.c_str(), eofMarker.size(), 0);
            }

        else if (command == "help"){
            string help_message = "Welcome to the FTP server!\n\n"
                                  "Available server commands:\n"
                                  "ls - List files in the current directory\n"
                                  "cd <directory> - Change directory\n"
                                  "chmod <mode> <file> - Change file permissions\n"
                                  "put <file> - Upload a file\n"
                                  "get <file> - Download a file\n"
                                  "close - Disconnect from server\n\n"
                                  "help - To view this help message\n\n"
                                  "Available client commands:\n"
                                  "lls - List files in the local directory\n"
                                  "lcd <directory> - Change local directory\n"
                                  "lchmod <mode> <file> - Change local file permissions\n";
            send(sock, help_message.c_str(), help_message.size(), 0);
        }

        else if (command.substr(0, 2) == "cd") {
            if (command.length() < 3) {
                send(sock, "Error changing directory\n", 24, 0);
                continue;
            }
            
            string path = command.substr(3);
            string new_path;
            if (!path.empty() && path[0] == '/') {
                new_path = path;
            }
            else if (!path.empty() && path[0] == '~') {
                const char* home = getenv("HOME");
                if (home == nullptr) {
                    send(sock, "Error changing directory\n", 24, 0);
                    continue;
                }
                new_path = string(home);
                if (path.length() > 1) {
                    if (path[1] != '/')
                        new_path += "/";
                    new_path += path.substr(1);
                }
            }
            else {
                new_path = client_directory + "/" + path;
            }
            struct stat statbuf;
            if (stat(new_path.c_str(), &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
                client_directory = new_path;
                send(sock, "Directory changed\n", 18, 0);
            } else {
                send(sock, "Error changing directory\n", 24, 0);
            }
     }

        else if (command.substr(0, 5) == "chmod"){
            string args = command.substr(6);
            size_t space = args.find(" ");
            if (space == string::npos){
                send(sock, "Invalid chmod command\n", 22, 0);
                continue;
            }

            string mode = args.substr(0, space);
            string filename = args.substr(space + 1);
            string filepath = client_directory + "/" + filename;

            // Use write lock for chmod (modifying file attributes)
            write_lock(&rwlock, filepath);
            bool success = (chmod(filepath.c_str(), stoi(mode, nullptr, 8)) == 0);
            write_unlock(&rwlock, filepath);

            if (success)
                send(sock, "Permissions changed\n", 20, 0);
            else
                send(sock, "Error changing permissions\n", 27, 0);
        }
        
        else if (command.substr(0, 3) == "put") {
            string filename = command.substr(4);
            string filepath = client_directory + "/" + filename;
            
            // Acquire exclusive (write) lock for the file
            write_lock(&rwlock, filepath);
            
            ofstream file(filepath, ios::binary);
            if (!file) {
                send(sock, "ERROR", 5, 0);
                write_unlock(&rwlock, filepath);
                continue;
            }
            
            send(sock, "OK", 2, 0);
            const uint32_t ERROR_SIGNAL = 0xFFFFFFFF;
            cout<<"Recieving "<<filename<<" ..."<<endl;
            
            int p = 1;
            while (true) {
                uint32_t net_chunk_size;
                if (!recv_all(sock, &net_chunk_size, sizeof(net_chunk_size))) {
                    cerr << "Header error" << endl;
                    p = 0;
                    break;
                }
                
                uint32_t chunk_size = ntohl(net_chunk_size);
                if (chunk_size == ERROR_SIGNAL) {
                    cerr << "Client aborted transfer" << endl;
                    p = 0;
                    break;
                }
                if (chunk_size == 0) break;  // EOF
                
                vector<char> buffer(chunk_size);
                if (!recv_all(sock, buffer.data(), chunk_size)) {
                    uint32_t net_err = htonl(ERROR_SIGNAL);
                    send_all(sock, &net_err, sizeof(net_err));
                    cerr << "Chunk error" << endl;
                    p = 0;
                    break;
                }
                file.write(buffer.data(), chunk_size);
            }
            
            file.close();
            if(p) cout<<"File recieved successfully\n"<<endl;
            
            // Release the write lock
            write_unlock(&rwlock, filepath);
        }
        
        else if (command.substr(0, 3) == "get") {
            string filename = command.substr(4);
            string filepath = client_directory + "/" + filename;
        
            // Acquire shared (read) lock for the file
            read_lock(&rwlock, filepath);
            
            ifstream file(filepath, ios::binary);
            if (!file) {
                send(sock, "NO", 2, 0);
                read_unlock(&rwlock, filepath);
                continue;
            }
        
            send(sock, "OK", 2, 0);
            const uint32_t ERROR_SIGNAL = 0xFFFFFFFF;
            char buffer[BUFFER_SIZE];
        
            cout << "Sending " << filename << " ...\n";
        
            while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
                uint32_t chunk_size = file.gcount();
                uint32_t net_chunk_size = htonl(chunk_size);
        
                if (!send_all(sock, &net_chunk_size, sizeof(net_chunk_size)) || 
                    !send_all(sock, buffer, chunk_size)) {
                    uint32_t net_err = htonl(ERROR_SIGNAL);
                    send_all(sock, &net_err, sizeof(net_err));
                    cout << "Error sending chunk. Aborted.\n";
                    break;
                }
            }
        
            uint32_t zero = htonl(0);
            send_all(sock, &zero, sizeof(zero));
            file.close();
            cout << "File sent successfully\n" << endl;
            
            // Release the read lock
            read_unlock(&rwlock, filepath);
        }        
        
        else if (command == "pwd"){
            char actualpath[PATH_MAX];
            if (realpath(client_directory.c_str(), actualpath) != NULL) {
                string response = string(actualpath);
                send(sock, response.c_str(), response.size(), 0);
            }
            else{
                send(sock,"~", 1, 0);
            }
        }
        
        else if (command == "close"){
            cout <<LBLUE<<"Client disconnected.\n";
            client_count--;
            cout<<"Clients remaining: "<<client_count<<RESET<<endl<<endl;
            send(sock, "Closing connection...\n\n", 23, 0);
            close(sock);
            pthread_exit(NULL);
        }

        else{
            cout<<RED<<"Invalid command: "<<command<<RESET<<endl<<endl;
            send(sock, "Invalid command\n", 16, 0);
        }
    }
}
