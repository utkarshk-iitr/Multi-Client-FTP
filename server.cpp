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
#include <fcntl.h>
#include <map>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <atomic>
#include <chrono>
#include <algorithm>

// #define port 8080
#define MAX_CLIENTS 8
#define BUFFER_SIZE 4096
#define LGREEN "\033[1;32m"
#define LBLUE "\033[1;34m"
#define RED "\033[1;31m"
#define YELLOW "\033[1;33m"
#define RESET "\033[0m"
#define LOCK_TIMEOUT_MS 5000     // 5 seconds lock timeout
#define MAX_LOCK_RETRIES 10      // Maximum number of retries before giving up
#define RETRY_DELAY_MS 100       // Initial retry delay (will increase with backoff)

int client_count = 0;
std::atomic<int> active_readers(0);
std::atomic<int> active_writers(0);

using namespace std;

// Global mutex for file operations
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Enhanced file lock structure with timeout and activity tracking
struct FileLock {
    pthread_mutex_t mutex;           // For internal lock state protection
    pthread_cond_t read_cv;          // Condition variable for readers
    pthread_cond_t write_cv;         // Condition variable for writers
    int readers;                     // Number of active readers
    bool writer_active;              // Is there an active writer?
    int waiting_readers;             // Number of waiting readers
    int waiting_writers;             // Number of waiting writers
    time_t last_accessed;            // Timestamp of last access
    int lock_count;                  // Total number of times this file was locked
    string current_locker;           // Information about current lock holder
    
    FileLock() : readers(0), writer_active(false), 
                 waiting_readers(0), waiting_writers(0), 
                 lock_count(0), current_locker("none") {
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&read_cv, NULL);
        pthread_cond_init(&write_cv, NULL);
        last_accessed = time(NULL);
    }
    
    ~FileLock() {
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&read_cv);
        pthread_cond_destroy(&write_cv);
    }
};

// Map to track file locks - maps filename to FileLock structure
map<string, FileLock*> file_locks;
pthread_mutex_t locks_mutex = PTHREAD_MUTEX_INITIALIZER;

// Stats for lock operations
struct LockStats {
    atomic<int> successful_read_locks;
    atomic<int> successful_write_locks;
    atomic<int> failed_read_locks;
    atomic<int> failed_write_locks;
    atomic<int> deadlock_preventions;
    atomic<int> timeouts;
    
    LockStats() : successful_read_locks(0), successful_write_locks(0),
                  failed_read_locks(0), failed_write_locks(0),
                  deadlock_preventions(0), timeouts(0) {}
};

LockStats lock_stats;

// Forward declaration
void *handle_client(void *client_socket);

// Function to get file permissions string like "drwxr-xr-x"
string get_permissions(mode_t mode) {
    string perms;
    perms += (S_ISDIR(mode)) ? 'd' : '-';
    
    // Owner permissions
    perms += (mode & S_IRUSR) ? 'r' : '-';
    perms += (mode & S_IWUSR) ? 'w' : '-';
    perms += (mode & S_IXUSR) ? 'x' : '-';
    
    // Group permissions
    perms += (mode & S_IRGRP) ? 'r' : '-';
    perms += (mode & S_IWGRP) ? 'w' : '-';
    perms += (mode & S_IXGRP) ? 'x' : '-';
    
    // Others permissions
    perms += (mode & S_IROTH) ? 'r' : '-';
    perms += (mode & S_IWOTH) ? 'w' : '-';
    perms += (mode & S_IXOTH) ? 'x' : '-';
    
    return perms;
}

// Function to format file size with appropriate units
string format_size(off_t size) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double display_size = size;
    
    while (display_size >= 1024 && unit_index < 4) {
        display_size /= 1024;
        unit_index++;
    }
    
    stringstream ss;
    ss << fixed << setprecision(1);
    if (unit_index == 0) {
        ss << setprecision(0); // No decimal for bytes
    }
    ss << display_size << " " << units[unit_index];
    return ss.str();
}

// Function to get a unique thread identifier string
string get_thread_id() {
    stringstream ss;
    ss << pthread_self();
    return ss.str();
}

// Enhanced function to acquire a read lock with timeout and priority handling
bool acquire_read_lock(const string& filepath, int timeout_ms = LOCK_TIMEOUT_MS) {
    pthread_mutex_lock(&locks_mutex);
    
    // Create a new lock if it doesn't exist
    if (file_locks.find(filepath) == file_locks.end()) {
        file_locks[filepath] = new FileLock();
    }
    
    FileLock* lock = file_locks[filepath];
    pthread_mutex_unlock(&locks_mutex);
    
    bool acquired = false;
    pthread_mutex_lock(&lock->mutex);
    
    // Update access time
    lock->last_accessed = time(NULL);
    
    // Check if we can acquire immediately
    if (!lock->writer_active && lock->waiting_writers == 0) {
        // No active writer and no waiting writers - we can proceed
        lock->readers++;
        lock->lock_count++;
        lock->current_locker = "reader:" + get_thread_id();
        acquired = true;
        lock_stats.successful_read_locks++;
        active_readers++;
    } else {
        // Wait with timeout if writers are active/waiting
        lock->waiting_readers++;
        
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }
        
        int wait_result = 0;
        while (!acquired && wait_result != ETIMEDOUT) {
            wait_result = pthread_cond_timedwait(&lock->read_cv, &lock->mutex, &ts);
            
            // If we're woken up, check if we can now acquire the lock
            if (wait_result == 0 && !lock->writer_active) {
                lock->readers++;
                lock->lock_count++;
                lock->current_locker = "reader:" + get_thread_id();
                acquired = true;
                lock_stats.successful_read_locks++;
                active_readers++;
            }
        }
        
        lock->waiting_readers--;
        
        if (!acquired) {
            // We failed to acquire the lock
            lock_stats.failed_read_locks++;
            if (wait_result == ETIMEDOUT) {
                lock_stats.timeouts++;
            }
        }
    }
    
    pthread_mutex_unlock(&lock->mutex);
    return acquired;
}

// Enhanced function to acquire a write lock with timeout
bool acquire_write_lock(const string& filepath, int timeout_ms = LOCK_TIMEOUT_MS) {
    pthread_mutex_lock(&locks_mutex);
    
    // Create a new lock if it doesn't exist
    if (file_locks.find(filepath) == file_locks.end()) {
        file_locks[filepath] = new FileLock();
    }
    
    FileLock* lock = file_locks[filepath];
    pthread_mutex_unlock(&locks_mutex);
    
    bool acquired = false;
    pthread_mutex_lock(&lock->mutex);
    
    // Update access time
    lock->last_accessed = time(NULL);
    
    // Check if we can acquire immediately
    if (!lock->writer_active && lock->readers == 0) {
        // No active writers or readers - we can proceed
        lock->writer_active = true;
        lock->lock_count++;
        lock->current_locker = "writer:" + get_thread_id();
        acquired = true;
        lock_stats.successful_write_locks++;
        active_writers++;
    } else {
        // Wait with timeout if other threads are active
        lock->waiting_writers++;
        
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }
        
        int wait_result = 0;
        while (!acquired && wait_result != ETIMEDOUT) {
            wait_result = pthread_cond_timedwait(&lock->write_cv, &lock->mutex, &ts);
            
            // If we're woken up, check if we can now acquire the lock
            if (wait_result == 0 && !lock->writer_active && lock->readers == 0) {
                lock->writer_active = true;
                lock->lock_count++;
                lock->current_locker = "writer:" + get_thread_id();
                acquired = true;
                lock_stats.successful_write_locks++;
                active_writers++;
            }
        }
        
        lock->waiting_writers--;
        
        if (!acquired) {
            // We failed to acquire the lock
            lock_stats.failed_write_locks++;
            if (wait_result == ETIMEDOUT) {
                lock_stats.timeouts++;
            }
        }
    }
    
    pthread_mutex_unlock(&lock->mutex);
    return acquired;
}

// Enhanced function to release a read lock
void release_read_lock(const string& filepath) {
    pthread_mutex_lock(&locks_mutex);
    
    if (file_locks.find(filepath) != file_locks.end()) {
        FileLock* lock = file_locks[filepath];
        pthread_mutex_lock(&lock->mutex);
        
        // Update access time
        lock->last_accessed = time(NULL);
        
        // Release the read lock
        if (lock->readers > 0) {
            lock->readers--;
            active_readers--;
            
            // If this was the last reader and writers are waiting, signal one writer
            if (lock->readers == 0 && lock->waiting_writers > 0) {
                pthread_cond_signal(&lock->write_cv);
            }
            
            // Clean up if no more activity
            if (lock->readers == 0 && !lock->writer_active && 
                lock->waiting_readers == 0 && lock->waiting_writers == 0) {
                pthread_mutex_unlock(&lock->mutex);
                pthread_mutex_destroy(&lock->mutex);
                pthread_cond_destroy(&lock->read_cv);
                pthread_cond_destroy(&lock->write_cv);
                delete lock;
                file_locks.erase(filepath);
                pthread_mutex_unlock(&locks_mutex);
                return;
            }
        }
        
        pthread_mutex_unlock(&lock->mutex);
    }
    
    pthread_mutex_unlock(&locks_mutex);
}

// Enhanced function to release a write lock
void release_write_lock(const string& filepath) {
    pthread_mutex_lock(&locks_mutex);
    
    if (file_locks.find(filepath) != file_locks.end()) {
        FileLock* lock = file_locks[filepath];
        pthread_mutex_lock(&lock->mutex);
        
        // Update access time
        lock->last_accessed = time(NULL);
        
        // Release the write lock
        if (lock->writer_active) {
            lock->writer_active = false;
            active_writers--;
            
            // Signal based on our policy - prefer writers unless too many readers waiting
            if (lock->waiting_writers > 0 && lock->waiting_readers < 10) {
                // Signal a waiting writer
                pthread_cond_signal(&lock->write_cv);
            } else if (lock->waiting_readers > 0) {
                // Signal all waiting readers
                pthread_cond_broadcast(&lock->read_cv);
            }
            
            // Clean up if no more activity
            if (lock->readers == 0 && !lock->writer_active && 
                lock->waiting_readers == 0 && lock->waiting_writers == 0) {
                pthread_mutex_unlock(&lock->mutex);
                pthread_mutex_destroy(&lock->mutex);
                pthread_cond_destroy(&lock->read_cv);
                pthread_cond_destroy(&lock->write_cv);
                delete lock;
                file_locks.erase(filepath);
                pthread_mutex_unlock(&locks_mutex);
                return;
            }
        }
        
        pthread_mutex_unlock(&lock->mutex);
    }
    
    pthread_mutex_unlock(&locks_mutex);
}

// Function to acquire a read lock with exponential backoff and deadlock prevention
bool acquire_read_lock_with_retry(const string& filepath) {
    int retries = 0;
    int delay_ms = RETRY_DELAY_MS;
    
    while (retries < MAX_LOCK_RETRIES) {
        if (acquire_read_lock(filepath)) {
            return true;
        }
        
        // Detect potential deadlock conditions
        if (active_writers > 0 && active_readers > 3 * MAX_CLIENTS) {
            lock_stats.deadlock_preventions++;
            cout << YELLOW << "Deadlock prevention: too many readers waiting for writers" << RESET << endl;
            usleep(delay_ms * 5 * 1000); // Wait 5x longer in deadlock situations
        }
        
        // Exponential backoff with jitter
        usleep(delay_ms * 1000);
        delay_ms = min(delay_ms * 2, 1000); // Cap at 1 second
        retries++;
    }
    
    return false;
}

// Function to acquire a write lock with exponential backoff and deadlock prevention
bool acquire_write_lock_with_retry(const string& filepath) {
    int retries = 0;
    int delay_ms = RETRY_DELAY_MS;
    
    while (retries < MAX_LOCK_RETRIES) {
        if (acquire_write_lock(filepath)) {
            return true;
        }
        
        // Detect potential deadlock conditions
        if (active_readers > 0 && active_writers > MAX_CLIENTS / 2) {
            lock_stats.deadlock_preventions++;
            cout << YELLOW << "Deadlock prevention: too many writers waiting for readers" << RESET << endl;
            usleep(delay_ms * 5 * 1000); // Wait 5x longer in deadlock situations
        }
        
        // Exponential backoff with jitter
        int jitter = rand() % 50; // Add up to 50ms of randomness
        usleep((delay_ms + jitter) * 1000);
        delay_ms = min(delay_ms * 2, 1000); // Cap at 1 second
        retries++;
    }
    
    return false;
}

// Function to print lock statistics
void print_lock_stats() {
    cout << LBLUE << "\n--- Lock Statistics ---" << RESET << endl;
    cout << "Active readers: " << active_readers << endl;
    cout << "Active writers: " << active_writers << endl;
    cout << "Successful read locks: " << lock_stats.successful_read_locks << endl;
    cout << "Successful write locks: " << lock_stats.successful_write_locks << endl;
    cout << "Failed read locks: " << lock_stats.failed_read_locks << endl;
    cout << "Failed write locks: " << lock_stats.failed_write_locks << endl;
    cout << "Deadlock preventions: " << lock_stats.deadlock_preventions << endl;
    cout << "Timeouts: " << lock_stats.timeouts << endl;
    
    // List of active locks
    pthread_mutex_lock(&locks_mutex);
    cout << "\nActive locks: " << file_locks.size() << endl;
    for (const auto& pair : file_locks) {
        pthread_mutex_lock(&pair.second->mutex);
        cout << "  " << pair.first << ": ";
        cout << "R=" << pair.second->readers << ", ";
        cout << "W=" << (pair.second->writer_active ? "1" : "0") << ", ";
        cout << "WR=" << pair.second->waiting_readers << ", ";
        cout << "WW=" << pair.second->waiting_writers << ", ";
        cout << "Count=" << pair.second->lock_count << ", ";
        cout << "By=" << pair.second->current_locker << endl;
        pthread_mutex_unlock(&pair.second->mutex);
    }
    pthread_mutex_unlock(&locks_mutex);
    cout << LBLUE << "----------------------" << RESET << endl;
}

// Thread function to periodically clean up abandoned locks
void* monitor_locks(void* arg) {
    while (true) {
        sleep(60); // Check every minute
        
        time_t now = time(NULL);
        vector<string> to_remove;
        
        pthread_mutex_lock(&locks_mutex);
        for (const auto& pair : file_locks) {
            pthread_mutex_lock(&pair.second->mutex);
            
            // If lock is inactive (no readers/writers/waiters) and hasn't been accessed in 10 minutes
            if (pair.second->readers == 0 && !pair.second->writer_active &&
                pair.second->waiting_readers == 0 && pair.second->waiting_writers == 0 &&
                difftime(now, pair.second->last_accessed) > 600) {
                
                to_remove.push_back(pair.first);
            }
            
            pthread_mutex_unlock(&pair.second->mutex);
        }
        
        // Remove abandoned locks
        for (const auto& path : to_remove) {
            FileLock* lock = file_locks[path];
            pthread_mutex_destroy(&lock->mutex);
            pthread_cond_destroy(&lock->read_cv);
            pthread_cond_destroy(&lock->write_cv);
            delete lock;
            file_locks.erase(path);
            cout << YELLOW << "Cleaned up abandoned lock: " << path << RESET << endl;
        }
        
        pthread_mutex_unlock(&locks_mutex);
        
        // Print lock statistics periodically
        if (!to_remove.empty() || file_locks.size() > 0) {
            print_lock_stats();
        }
    }
    
    return NULL;
}

bool is_dir(const string path) {
    struct stat path_stat;
    if (stat(path.c_str(), &path_stat) != 0) {
        return false;
    }
    return S_ISDIR(path_stat.st_mode);
}

// Check if file/directory exists
bool file_exists(const string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
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

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./server <port>" <<endl;
        return 1;
    }
    int port = atoi(argv[1]);

    if (port <= 1024 || port > 65535) { 
        cerr << "Please provide a valid port number in the range 1025-65535.\n";
        return 1;
    }

    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Reuse failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    // Initialize random seed for jitter in backoff algorithm
    srand(time(NULL));
    
    // Start lock monitor thread
    pthread_t monitor_thread;
    if (pthread_create(&monitor_thread, NULL, monitor_locks, NULL) != 0) {
        cerr << "Failed to create lock monitor thread" << endl;
    } else {
        pthread_detach(monitor_thread);
    }

    string myip = getip();
    if (myip == "-1") {
        cout << "Error in getting IP" << endl;
        return 0;
    }
    cout << "FTP Server started at " << myip << ":" << port << " ...\n" << endl;

    while (true) {
        client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0) {
            perror("Client accept failed");
            continue;
        }

        pthread_t thread_id;
        client_count++;
        
        // Create a copy of the socket descriptor for the thread
        int* client_sock = new int;
        *client_sock = client_socket;
        
        pthread_create(&thread_id, NULL, handle_client, (void *)client_sock);
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}

// Handle ls command with options
void handle_ls(int sock, const string& args, const string& client_directory) {
    bool show_hidden = false;
    bool long_format = false;
    string target_dir = client_directory;
    
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
            string path = (arg[0] == '/') ? arg : client_directory + "/" + arg;
            if (is_dir(path)) {
                target_dir = path;
            } else {
                string error_msg = "ls: cannot access '" + arg + "': No such file or directory\n";
                send(sock, error_msg.c_str(), error_msg.size(), 0);
                string eofMarker = "EOF\n";
                send(sock, eofMarker.c_str(), eofMarker.size(), 0);
                return;
            }
        }
    }
    
    // Acquire a read lock on the directory for consistent listing
    if (!acquire_read_lock_with_retry(target_dir)) {
        string error_msg = "ls: Unable to acquire lock on directory. Server busy, try again later.\n";
        send(sock, error_msg.c_str(), error_msg.size(), 0);
        string eofMarker = "EOF\n";
        send(sock, eofMarker.c_str(), eofMarker.size(), 0);
        return;
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
                string full_path = target_dir + "/" + file_name;
                struct stat file_stat;
                
                if (stat(full_path.c_str(), &file_stat) == 0) {
                    string perms = get_permissions(file_stat.st_mode);
                    
                    // Get user and group names
                    struct passwd *pw = getpwuid(file_stat.st_uid);
                    struct group *gr = getgrgid(file_stat.st_gid);
                    string username = pw ? pw->pw_name : to_string(file_stat.st_uid);
                    string groupname = gr ? gr->gr_name : to_string(file_stat.st_gid);
                    
                    // Format time
                    char time_str[30];
                    strftime(time_str, sizeof(time_str), "%b %d %H:%M", localtime(&file_stat.st_mtime));
                    
                    // Format output line
                    stringstream line;
                    line << perms << " ";
                    line << setw(3) << right << file_stat.st_nlink << " ";
                    line << setw(8) << left << username << " ";
                    line << setw(8) << left << groupname << " ";
                    line << setw(8) << right << format_size(file_stat.st_size) << " ";
                    line << time_str << " ";
                    line << file_name;
                    
                    string entry_line = line.str() + "\n";
                    send(sock, entry_line.c_str(), entry_line.size(), 0);
                }
            } else {
                string entry_line = file_name + "\n";
                send(sock, entry_line.c_str(), entry_line.size(), 0);
            }
        }
    } else {
        string error_msg = "Error opening directory\n";
        send(sock, error_msg.c_str(), error_msg.size(), 0);
    }
    
    // Release the read lock
    release_read_lock(target_dir);
    
    // Send end-of-transmission marker
    string eofMarker = "EOF\n";
    send(sock, eofMarker.c_str(), eofMarker.size(), 0);
}

void *handle_client(void *client_socket) {
    int sock = *(int *)client_socket;
    delete (int*)client_socket;  // Free the allocated memory
    
    char buffer[BUFFER_SIZE];
    string client_directory = ".";
    cout << LGREEN << "Clients connected: " << client_count << RESET << endl << endl;

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        if (recv(sock, buffer, BUFFER_SIZE, 0) <= 0) {
            cout << LBLUE << "Client disconnected.\n";
            client_count--;
            cout << "Clients remaining: " << client_count << RESET << endl << endl;
            close(sock);
            pthread_exit(NULL);
        }

        string command_input(buffer);
        command_input = command_input.substr(0, command_input.find("\n"));
        
        // Parse command and arguments
        string command, args;
        parse_command(command_input, command, args);

        if (command == "ls") {
            handle_ls(sock, args, client_directory);
        }

        else if (command == "help") {
            string help_message = "Welcome to the FTP server!\n\n"
                                "Available server commands:\n"
                                "ls [-l] [-a] [directory] - List files in the current directory\n"
                                "  -l: Use long listing format\n"
                                "  -a: Show hidden files\n"
                                "cd <directory> - Change directory\n"
                                "chmod <mode> <file> - Change file permissions\n"
                                "put <file> - Upload a file\n"
                                "get <file> - Download a file\n"
                                "pwd - Print working directory\n"
                                "status - Show server lock statistics\n"
                                "close - Disconnect from server\n\n"
                                "help - To view this help message\n\n"
                                "Available client commands:\n"
                                "lls - List files in the local directory\n"
                                "lcd <directory> - Change local directory\n"
                                "lchmod <mode> <file> - Change local file permissions\n"
                                "lpwd - Print local working directory\n";
            send(sock, help_message.c_str(), help_message.size(), 0);
        }

        else if (command == "cd") {
            if (args.empty()) {
                send(sock, "Error changing directory\n", 24, 0);
                continue;
            }
            
            string path = args;
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
            
            // Acquire read lock to check directory
            if (!acquire_read_lock_with_retry(new_path)) {
                send(sock, "Error: Directory busy, try again later\n", 38, 0);
                continue;
            }
            
            struct stat statbuf;
            bool success = false;
            
            if (stat(new_path.c_str(), &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
                client_directory = new_path;
                send(sock, "Directory changed\n", 18, 0);
                success = true;
            } else {
                send(sock, "Error changing directory\n", 24, 0);
            }
            
            release_read_lock(new_path);
        }

        else if (command == "chmod") {
            // Parse chmod arguments
            stringstream ss(args);
            string mode_str, filename;
            ss >> mode_str >> filename;
            
            if (mode_str.empty() || filename.empty()) {
                send(sock, "Usage: chmod <mode> <file>\n", 26, 0);
                continue;
            }
            
            string filepath = client_directory + "/" + filename;
            if (!file_exists(filepath)) {
                string error_msg = "chmod: cannot access '" + filename + "': No such file or directory\n";
                send(sock, error_msg.c_str(), error_msg.size(), 0);
                continue;
            }

            // Get write lock for the file
            if (!acquire_write_lock_with_retry(filepath)) {
                send(sock, "Error: File busy, try again later\n", 33, 0);
                continue;
            }

            try {
                if (chmod(filepath.c_str(), stoi(mode_str, nullptr, 8)) == 0)
                    send(sock, "Permissions changed\n", 20, 0);
                else
                    send(sock, "Error changing permissions\n", 27, 0);
            } catch (const exception& e) {
                string error_msg = "Invalid mode: " + mode_str + "\n";
                send(sock, error_msg.c_str(), error_msg.size(), 0);
            }
            
            release_write_lock(filepath);
        }
        
        else if (command == "put") {
            if (args.empty()) {
                send(sock, "Usage: put <filename>\n", 21, 0);
                continue;
            }
            
            string filename = args;
            string filepath = client_directory + "/" + filename;
            
            // Check if target is a directory
            if (is_dir(filepath)) {
                send(sock, "ERROR_DIR", 9, 0);
                cout << RED << "Client attempted to upload directory: " << filename << RESET << endl;
                continue;
            }
            
            // Acquire write lock for file uploading
            if (!acquire_write_lock_with_retry(filepath)) {
                send(sock, "ERROR", 5, 0);
                cout << RED << "Upload rejected for " << filename << ": file is locked by another client" << RESET << endl;
                continue;
            }
            
            ofstream file(filepath, ios::binary);
            if (!file) {
                send(sock, "ERROR", 5, 0);
                release_write_lock(filepath);
                continue;
            }
            
            send(sock, "OK", 2, 0);
            const uint32_t ERROR_SIGNAL = 0xFFFFFFFF;
            cout << "Receiving " << filename << " ..." << endl;
            
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
            if (p) cout << "File received successfully\n" << endl;
            
            // Release the file lock
            release_write_lock(filepath);
        }
        
        else if (command == "get") {
            if (args.empty()) {
                send(sock, "Usage: get <filename>\n", 21, 0);
                continue;
            }
            
            string filename = args;
            string filepath = client_directory + "/" + filename;
            
            // Check if file exists
            if (!file_exists(filepath)) {
                send(sock, "NO", 2, 0);
                continue;
            }
            
            // Check if path is a directory
            if (is_dir(filepath)) {
                send(sock, "DIR", 3, 0);
                cout << RED << "Client attempted to download directory: " << filename << RESET << endl;
                continue;
            }
            
            // Acquire read lock for file downloading
            if (!acquire_read_lock_with_retry(filepath)) {
                send(sock, "NO", 2, 0);
                cout << RED << "Download rejected for " << filename << ": file is locked for writing" << RESET << endl;
                continue;
            }
            
            ifstream file(filepath, ios::binary);
            if (!file) {
                send(sock, "NO", 2, 0);
                release_read_lock(filepath);
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
            
            // Release the file lock
            release_read_lock(filepath);
        }        
        
        else if (command == "pwd") {
            char actualpath[PATH_MAX];
            if (realpath(client_directory.c_str(), actualpath) != NULL) {
                string response = string(actualpath);
                send(sock, response.c_str(), response.size(), 0);
            }
            else {
                send(sock, "~", 1, 0);
            }
        }
        
        else if (command == "status") {
            // Capture lock statistics in a string
            stringstream ss;
            ss << "\n--- Lock Statistics ---\n";
            ss << "Active readers: " << active_readers << "\n";
            ss << "Active writers: " << active_writers << "\n";
            ss << "Successful read locks: " << lock_stats.successful_read_locks << "\n";
            ss << "Successful write locks: " << lock_stats.successful_write_locks << "\n";
            ss << "Failed read locks: " << lock_stats.failed_read_locks << "\n";
            ss << "Failed write locks: " << lock_stats.failed_write_locks << "\n";
            ss << "Deadlock preventions: " << lock_stats.deadlock_preventions << "\n";
            ss << "Timeouts: " << lock_stats.timeouts << "\n";
            
            // Active locks
            ss << "\nActive locks: " << file_locks.size() << "\n";
            pthread_mutex_lock(&locks_mutex);
            for (const auto& pair : file_locks) {
                pthread_mutex_lock(&pair.second->mutex);
                ss << "  " << pair.first << ": ";
                ss << "R=" << pair.second->readers << ", ";
                ss << "W=" << (pair.second->writer_active ? "1" : "0") << ", ";
                ss << "WR=" << pair.second->waiting_readers << ", ";
                ss << "WW=" << pair.second->waiting_writers << ", ";
                ss << "Count=" << pair.second->lock_count << ", ";
                ss << "By=" << pair.second->current_locker << "\n";
                pthread_mutex_unlock(&pair.second->mutex);
            }
            pthread_mutex_unlock(&locks_mutex);
            ss << "----------------------\n";
            
            string stats = ss.str();
            send(sock, stats.c_str(), stats.size(), 0);
        }
        
        else if (command == "close") {
            cout << LBLUE << "Client disconnected.\n";
            client_count--;
            cout << "Clients remaining: " << client_count << RESET << endl << endl;
            send(sock, "Closing connection...\n\n", 23, 0);
            close(sock);
            pthread_exit(NULL);
        }

        else {
            cout << RED << "Invalid command: " << command << RESET << endl << endl;
            send(sock, "Invalid command\n", 16, 0);
        }
    }
}
