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

// #define port 8080
#define MAX_CLIENTS 8
#define BUFFER_SIZE 4096
#define LGREEN "\033[1;32m"
#define LBLUE "\033[1;34m"
#define RED "\033[1;31m"
#define RESET "\033[0m"
int client_count = 0;

using namespace std;

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

int main(int argc, char* argv[]){

    if (argc != 2) {
        cerr << "Usage: ./server <port>" <<endl;
        return 1;
    }
    int port = atoi(argv[1]);

    if (port <= 1024 || port > 65535) { 
        // Note: Ports below 1025 are often reserved for privileged processes.
        cerr << "Please provide a valid port number in the range 1025-65535.\n";
        return 1;
    }

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
            vector<string> response;
            string ans= "";

            if ((dir = opendir(client_directory.c_str())) != NULL){
                while ((entry = readdir(dir)) != NULL){
                    response.push_back(string(entry->d_name));
                }
                closedir(dir);
            }
            else{
                ans = "Error opening directory\n";
                send(sock, ans.c_str(), ans.size(), 0);
                continue;
            }

            sort(response.begin(), response.end());
            for (const auto &file : response){
                if (file != "." && file != ".."){
                    ans += file + "\n";
                }
            }
            send(sock, ans.c_str(), ans.size(), 0);
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

        else if (command.substr(0, 2) == "cd"){
            string path = command.substr(3);
            string new_path = client_directory + "/" + path;

            struct stat statbuf;
            if (stat(new_path.c_str(), &statbuf) == 0 && S_ISDIR(statbuf.st_mode)){
                client_directory = new_path;
                send(sock, "Directory changed\n", 18, 0);
            }

            else{
                send(sock, "Error changing directory\n", 25, 0);
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

            if (chmod(filepath.c_str(), stoi(mode, nullptr, 8)) == 0)
                send(sock, "Permissions changed\n", 20, 0);
            else
                send(sock, "Error changing permissions\n", 27, 0);
        }

        else if (command.substr(0, 3) == "put"){
            pthread_mutex_lock(&file_mutex);
            string filename = command.substr(4);
            string filepath = client_directory + "/" + filename;

            ofstream file(filepath, ios::binary);
            if (!file){
                send(sock, "ERROR", 5, 0);
                pthread_mutex_unlock(&file_mutex);
                continue;
            }

            send(sock, "File opened\n", 12, 0);
            int total_received = 0;
            cout<<"Receiving..."<<endl;

            while (true){
                int bytes = recv(sock, buffer, BUFFER_SIZE, 0);
                if (bytes <= 0){
                    cout << "Error receiving file data or connection closed.\n";
                    pthread_mutex_unlock(&file_mutex);
                    break;
                }

                send(sock, "OK", 2, 0);
                string data(buffer, bytes);
                                
                if(strcmp(buffer, "EOFEOFEOFEOF") == 0){
                    // cout << "End of file reached.\n" <<endl;
                    pthread_mutex_unlock(&file_mutex);
                    break;
                }
                else if (bytes == 0){
                    // cout << "End of file reached.\n" <<endl;
                    pthread_mutex_unlock(&file_mutex);
                    break;
                }

                file.write(buffer, bytes);
                total_received += bytes;
                // cout << "Received: " << total_received << " bytes\n";
                memset(buffer, 0, BUFFER_SIZE);
            }

            file.close();
            cout << "File recieved successfully.\n"<<endl;
            pthread_mutex_unlock(&file_mutex);
        }

        else if (command.substr(0, 3) == "get"){
            pthread_mutex_lock(&file_mutex);
            string filename = command.substr(4);
            string filepath = client_directory + "/" + filename;

            if(is_dir(filepath)){
                send(sock, "WRONG", 5, 0);
                pthread_mutex_unlock(&file_mutex);
                continue;
            }

            ifstream file(filepath, ios::binary);
            if (!file){
                send(sock, "ERROR", 5, 0);
                pthread_mutex_unlock(&file_mutex);
                continue;
            }

            send(sock, "File opened\n", 12, 0);
            char buffer[BUFFER_SIZE];
            char buffer2[BUFFER_SIZE];

            cout<<"Sending..."<<endl;
            while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0){
                int bytes_sent = send(sock, buffer, file.gcount(), 0);
                memset(buffer2, 0, BUFFER_SIZE);
                recv(sock, buffer2, BUFFER_SIZE, 0);
                
                if(strncmp(buffer2,"OK",2)!=0){
                    cout << "Error receiving acknowledgment"<<buffer2<<endl;
                    // exit(0);
                }
                
                if (bytes_sent <= 0){
                    cout << "Error sending file data\n";
                    break;
                }
                memset(buffer2, 0, BUFFER_SIZE);
                // cout<<buffer2<<endl;
                memset(buffer, 0, BUFFER_SIZE);
            }
            
            send(sock, "EOFEOFEOFEOF", 12, 0);
            memset(buffer2, 0, BUFFER_SIZE);
            recv(sock, buffer2, BUFFER_SIZE, 0);
            if(strcmp(buffer2, "OK") != 0){
                cout << "Error receiving acknowledgment " <<buffer2<< endl;
            }
            
            cout << "File sending completed.\n"<<endl;
            file.close();
            pthread_mutex_unlock(&file_mutex);
        }

        else if (command == "prompt"){
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
