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

string read_ack(int sock) {
    string ack = "";
    char ch;
    while (recv(sock, &ch, 1, 0) == 1) {
        if (ch == '\n') break;
        ack += ch;
    }
    return ack;
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
            // cout << "Closing connection...\n";
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
        return;
    }
    
    // Inform the server of the command (unchanged)
    string command = "put " + filename;
    send(sock, command.c_str(), command.size(), 0);
    
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    if (strcmp(buffer, "ERROR") == 0) {
        cout << "Error opening file" << endl;
        return;
    }
    
    cout << "Sending..." << endl;
    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        uint32_t chunk_size = file.gcount();
        uint32_t net_chunk_size = htonl(chunk_size);  // Convert to network byte order
        
        // First, send the size header
        if (send(sock, &net_chunk_size, sizeof(net_chunk_size), 0) != sizeof(net_chunk_size)) {
            cout << "Error sending chunk size" << endl;
            break;
        }
        // Then, send the chunk data
        if (send(sock, buffer, chunk_size, 0) != (int)chunk_size) {
            cout << "Error sending file data" << endl;
            break;
        }
    }
    // Send a final header with 0 length to signal end of file
    uint32_t zero = 0;
    zero = htonl(zero);
    send(sock, &zero, sizeof(zero), 0);
    cout << "File transfer complete" << endl;
    file.close();
}  

void handle_get(int sock, string filename) {
    string command = "get " + filename;
    send(sock, command.c_str(), command.size(), 0);

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    // Receive initial server response (e.g. "File opened\n")
    int ret = recv(sock, buffer, BUFFER_SIZE, 0);
    if (ret <= 0) {
        cout << "Connection closed or error occurred." << endl;
        return;
    }
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        cout << "No such file exists" << endl;
        return;
    } 
    else if (strncmp(buffer, "WRONG", 5) == 0) {
        cout << "Error: Cannot download a directory" << endl;
        return;
    }
    
    ofstream file(filename, ios::binary);
    if (!file) {
        cout << "Error creating file: " << filename << endl;
        return;
    }
    
    cout << "Receiving..." << endl;
    
    // Loop to receive each chunk size header then the chunk data.
    while (true) {
        uint32_t net_chunk_size;
        // Read the 4-byte header for the chunk size
        int header_bytes = 0;
        char* header_ptr = reinterpret_cast<char*>(&net_chunk_size);
        while (header_bytes < sizeof(net_chunk_size)) {
            int n = recv(sock, header_ptr + header_bytes, sizeof(net_chunk_size) - header_bytes, 0);
            if (n <= 0) {
                cout << "Error receiving chunk size (connection closed?)" << endl;
                file.close();
                return;
            }
            header_bytes += n;
        }
        uint32_t chunk_size = ntohl(net_chunk_size);  // Convert from network to host byte order
        
        // A header of 0 means end-of-file
        if (chunk_size == 0) {
            break;
        }
            
        int total_received = 0;
        // Ensure we receive exactly chunk_size bytes for this chunk
        while (total_received < (int)chunk_size) {
            int to_read = min(BUFFER_SIZE, (int)(chunk_size - total_received));
            int bytes = recv(sock, buffer, to_read, 0);
            if (bytes <= 0) {
                cout << "Error receiving file data or connection closed." << endl;
                file.close();
                return;
            }
            file.write(buffer, bytes);
            total_received += bytes;
        }
    }
    
    file.close();
    cout << "File downloaded successfully" << endl;
}