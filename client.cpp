#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <bits/stdc++.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
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

int main(){
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

void handle_put(int sock, string filename){
    ifstream file(filename, ios::binary);
    if (!file){
        cout << "File not found: " << filename << endl;
        return;
    }

    char buffer[BUFFER_SIZE];
    string command = "put " + filename;
    send(sock, command.c_str(), command.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);

    if (strcmp(buffer, "ERROR") == 0){
        cout << "Error opening file" << endl;
        return;
    }

    memset(buffer, 0, BUFFER_SIZE);
    char buffer2[BUFFER_SIZE];
    memset(buffer2, 0, BUFFER_SIZE);

    cout<<"Sending..."<<endl;
    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0){
        int bytes_sent = send(sock, buffer, file.gcount(), 0);
        recv(sock, buffer2, BUFFER_SIZE, 0);
        // cout<<"Server Acknowledgement: "<<buffer2<<endl;

        if(strcmp(buffer2, "OK") != 0){
            cout << "Error receiving acknowledgment\n" << endl;
        }
        
        // cout<<"buffer="<<buffer2<<endl;
        if (bytes_sent <= 0){
            cout << "Error sending file data\n" <<endl;
            break;
        }
        memset(buffer2, 0, BUFFER_SIZE);
        memset(buffer, 0, BUFFER_SIZE);
    }

    send(sock, "EOFEOFEOFEOF\n", 12, 0);
    recv(sock, buffer2, BUFFER_SIZE, 0);

    if(strcmp(buffer2, "OK") != 0){
        cout << "Error receiving acknowledgment\n" << endl;
    }
    else{
        cout << "File transfer complete"<<endl;
    }
    file.close();
}

void handle_get(int sock, string filename){
    string command = "get " + filename;
    char buffer[BUFFER_SIZE];
    send(sock, command.c_str(), command.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);

    if(strcmp(buffer, "ERROR") == 0){
        cout << "No such file exists" << endl;
        return;
    }
    else if(strcmp(buffer, "WRONG") == 0){
        cout << "Error: Cannot download a directory" << endl;
        return;
    }
    memset(buffer, 0, BUFFER_SIZE);

    ofstream file(filename, ios::binary);
    if (!file){
        cout << "Error creating file: " << filename << endl;
        return;
    }

    cout<<"Receiving..."<<endl;
    int total_received = 0;

    while (true){   
        int bytes = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes <= 0){
            cout << "Error receiving file data or connection closed.\n";
            break;
        }

        send(sock, "OK", 2, 0);
        string data(buffer, bytes);
                
        if(strcmp(buffer, "EOFEOFEOFEOF") == 0) break;
        else if (bytes == 0) break;
        file.write(buffer, bytes);
        total_received += bytes;
        memset(buffer, 0, BUFFER_SIZE);
    }

    file.close();
    cout << "File downloaded successfully"<<endl;
}

