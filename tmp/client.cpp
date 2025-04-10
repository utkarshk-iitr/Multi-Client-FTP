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

#define BUFFER_SIZE 4096
#define LGREEN "\033[1;32m"
#define LBLUE "\033[1;34m"
#define RESET "\033[0m"

using namespace std;

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
        send_command(sock, "prompt");
        cout<<RESET<<" > ";
        string input;
        getline(cin, input);
        if (input.empty())
            continue;

        string command = input.substr(0, input.find(" "));
        string arg = (input.find(" ") != string::npos) ? input.substr(input.find(" ") + 1) : "";

        if (command == "lls"){
            DIR *dir;
            struct dirent *entry;
            vector<string> response;

            if ((dir = opendir(".")) != NULL){
                while ((entry = readdir(dir)) != NULL){
                    response.push_back(string(entry->d_name));
                }
                closedir(dir);
            }
            else{
                cout << "Error listing directory\n";
                continue;
            }

            sort(response.begin(), response.end());
            for (const auto &file : response){
                if (file != "." && file != ".."){
                    cout<<file<<endl;
                }
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

void handle_put(int sock, string filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cout << "File not found: " << filename << endl;
        send(sock, "ERROR", 5, 0);  // Notify server
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
    }
    
    // Send EOF marker
    uint32_t zero = 0;
    zero = htonl(zero);
    send_all(sock, &zero, sizeof(zero));
    if(p) cout << "Transfer complete" << endl;
    file.close();
}

// Modified handle_get function
void handle_get(int sock, string filename) {
    string command = "get " + filename;
    send(sock, command.c_str(), command.size(), 0);

    char response[BUFFER_SIZE];
    recv(sock, response, BUFFER_SIZE, 0);
    if (strncmp(response, "ERROR", 5) == 0) {
        cout << "File not found on server" << endl;
        return;
    }

    ofstream file(filename, ios::binary);
    if (!file) {
        cout << "Local file error" << endl;
        return;
    }
    
    cout<<"Recieving "<<filename<<" ..."<<endl;
    const uint32_t ERROR_SIGNAL = 0xFFFFFFFF;
    int  p = 1;

    while (true) {
        uint32_t net_chunk_size;
        if (!recv_all(sock, &net_chunk_size, sizeof(net_chunk_size))) {
            cout << "Connection lost" << endl;
            p = 0;
            break;
        }

        uint32_t chunk_size = ntohl(net_chunk_size);
        if (chunk_size == ERROR_SIGNAL) {
            cout << "Server reported error" << endl;
            p = 0;
            break;
        }
        if (chunk_size == 0) break;  // EOF

        vector<char> buffer(chunk_size);
        if (!recv_all(sock, buffer.data(), chunk_size)) {
            cout << "Incomplete chunk" << endl;
            p = 0; 
            break;
        }
        file.write(buffer.data(), chunk_size);
    }

    if(p) cout << "File downloaded" << endl;
    file.close();
}