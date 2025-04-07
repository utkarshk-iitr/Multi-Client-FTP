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

#define PORT 8080
#define MAX_CLIENTS 8
#define BUFFER_SIZE 4096
int client_count = 0;

using namespace std;

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
void *handle_client(void *client_socket);

int main()
{
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
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0){
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    cout << "FTP Server started on port " << PORT << "...\n";

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

void *handle_client(void *client_socket)
{
    int sock = *(int *)client_socket;
    char buffer[BUFFER_SIZE];
    string client_directory = ".";
    cout<<"Clients connected: "<<client_count<<endl<<endl;

    while (true){
        memset(buffer, 0, BUFFER_SIZE);
        if (recv(sock, buffer, BUFFER_SIZE, 0) <= 0){
            cout << "Client disconnected.\n";
            client_count--;
            cout<<"Clients remaining: "<<client_count<<endl<<endl;
            close(sock);
            pthread_exit(NULL);
        }

        string command(buffer);
        command = command.substr(0, command.find("\n")); // Remove newline

        if (command == "ls"){
            DIR *dir;
            struct dirent *entry;
            string response = "";

            if ((dir = opendir(client_directory.c_str())) != NULL){
                while ((entry = readdir(dir)) != NULL){
                    response += string(entry->d_name) + "\n";
                }
                closedir(dir);
            }

            else{
                response = "Error opening directory\n";
            }
            send(sock, response.c_str(), response.size(), 0);
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
                recv(sock, buffer2, BUFFER_SIZE, 0);
                // cout<<"Client Acknowledgement: "<<buffer2<<endl;

                if(strcmp(buffer2, "OK") != 0){
                    cout << "Error receiving acknowledgment\n" << endl;
                }
                
                // cout<<"buffer="<<buffer2<<endl;
                if (bytes_sent <= 0){
                    cout << "Error sending file data\n";
                    break;
                }
                memset(buffer2, 0, BUFFER_SIZE);
                memset(buffer, 0, BUFFER_SIZE);
            }
            // cout<<"sending eof"<<endl;
            send(sock, "EOFEOFEOFEOF\n", 12, 0);
            recv(sock, buffer2, BUFFER_SIZE, 0);

            if(strcmp(buffer2, "OK") != 0){
                cout << "Error receiving acknowledgment\n" << endl;
            }
            
            cout << "File sending completed.\n"<<endl;
            file.close();
            pthread_mutex_unlock(&file_mutex);
        }

        else if (command == "close"){
            cout << "Client disconnected.\n";
            client_count--;
            cout<<"Clients remaining: "<<client_count<<endl<<endl;
            send(sock, "Closing connection...\n", 22, 0);
            close(sock);
            pthread_exit(NULL);
        }

        else{
            cout<<"Invalid command: "<<command<<endl;
            send(sock, "Invalid command\n", 16, 0);
        }
    }
}
