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

#define BUFFER_SIZE 4096
#define RESET   "\033[0m"
#define RED     "\033[31m"      // Red text
#define GREEN   "\033[32m"      // Green text
#define YELLOW  "\033[33m"      // Yellow text
#define BLUE    "\033[34m"      // Blue text
#define MAGENTA "\033[35m"      // Magenta text
#define CYAN    "\033[36m"      // Cyan text
#define BOLD    "\033[1m"       // Bold text
#define LGREEN "\033[92m"
#define LBLUE  "\033[94m"
using namespace std;

string send_command(int sock, const string& command);
void handle_put(int sock, const string& filename);
void handle_get(int sock, const string& filename);

int main(int argc, char *argv[])
{
    if (argc != 3){
        cerr << "Usage: " << argv[0] << " <server_ip> <port>\n";
        return EXIT_FAILURE;
    }

    const char* SERVER_IP = argv[1];
    int PORT = atoi(argv[2]);
    if (PORT <= 0 || PORT > 65535) {
        cerr << "Invalid port: " << argv[2] << "\n";
        return EXIT_FAILURE;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) != 1) 
    {
        cerr << "Invalid IP address: " << SERVER_IP << "\n";
        close(sock);
        return EXIT_FAILURE;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
    {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    cout << "Connected to FTP server at " << SERVER_IP << ":" << PORT << endl;
    send_command(sock, "cd .");
    string server_dir;
    while (true) {
        
        server_dir = send_command(sock, "pwd");

        cout <<LGREEN<< "ftp: " <<LBLUE<< server_dir << "> "<<RESET;
        string input;
        if (!getline(cin, input) || input.empty())
            continue;

        string command = input.substr(0, input.find(' '));
        string arg     = (input.find(' ') != string::npos)
                         ? input.substr(input.find(' ') + 1)
                         : "";

        if (command == "lls") {
            DIR *dir;
            struct dirent *entry;
            if ((dir = opendir(".")) != nullptr) {
                while ((entry = readdir(dir)) != nullptr) {
                    cout << entry->d_name << "\n";
                }
                closedir(dir);
            } else {
                cout << "Error listing directory\n";
            }
        }
        else if (command == "lpwd") {
            char buffer[FILENAME_MAX];

            if (getcwd(buffer, sizeof(buffer)) != nullptr) {
                cout << "Local working directory: " << buffer << std::endl;
            } else {
                perror("getcwd error");
                return 1;
            }
        }
        else if (command == "lcd") {
            if (chdir(arg.c_str()) == 0)
                cout << "Directory changed\n";
            else
                cout << "Error changing directory\n";
        }
        else if (command == "pwd") {
            // ask the server for its current directory
            cout<<send_command(sock, "pwd")<<endl;
        }    
        else if (command == "lchmod") {
            size_t space = arg.find(' ');
            if (space == string::npos) {
                cout << "Usage: lchmod <mode> <file>\n";
                continue;
            }
            string mode     = arg.substr(0, space);
            string filename = arg.substr(space + 1);
            if (chmod(filename.c_str(), stoi(mode, nullptr, 8)) == 0)
                cout << "Permissions changed\n";
            else
                cout << "Error changing permissions\n";
        }
        else if (command == "put") {
            handle_put(sock, arg);
        }
        else if (command == "get") {
            handle_get(sock, arg);
        }
        else if (command == "close") {
            send_command(sock, "close");
            cout << "Closing connection...\n";
            break;
        }
        else {
            // cout<<"hello"<<endl;
            string response = send_command(sock, input);
            cout << response << endl;
            // cout << "Unknown command: " << command << "\n";
        }
    }

    close(sock);
    return 0;
}

string send_command(int sock, const string& command)
{
    char buffer[BUFFER_SIZE];
    send(sock, command.c_str(), command.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    return buffer;
}

void handle_put(int sock, const string& filename)
{
    ifstream file(filename, ios::binary);
    if (!file) {
        cout << "File not found: " << filename << "\n";
        return;
    }

    char buffer[BUFFER_SIZE];
    string cmd = "put " + filename;
    send(sock, cmd.c_str(), cmd.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);

    if (strcmp(buffer, "Error opening file") == 0) {
        cout << buffer << "\n";
        return;
    }

    cout << "Sending...\n";
    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        int bytes_sent = send(sock, buffer, file.gcount(), 0);
        recv(sock, buffer, BUFFER_SIZE, 0);
        // cout << "Server Ack: " << buffer;
        if (bytes_sent <= 0) {
            cout << "Error sending file data\n";
            break;
        }
        memset(buffer, 0, BUFFER_SIZE);
    }

    send(sock, "EOFEOFEOFEOF\n", 12, 0);
    recv(sock, buffer, BUFFER_SIZE, 0);
    // cout << "Server Ack: " << buffer<<endl;
    if(strcmp(buffer, "Received") != 0)
        cout << "Error receiving acknowledgment\n";
    else
        cout << "File transfer complete.\n";
    file.close();
}

void handle_get(int sock, const string& filename)
{
    string cmd = "get " + filename;
    char buffer[BUFFER_SIZE];
    send(sock, cmd.c_str(), cmd.size(), 0);

    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    if (strcmp(buffer, "No such file exists") == 0) {
        cout << buffer << "\n";
        return;
    }

    ofstream file(filename, ios::binary);
    if (!file) {
        cout << "Error creating file: " << filename << "\n";
        return;
    }

    cout << "Receiving...\n";
    while (true) {
        int bytes = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes <= 0) {
            cout << "Error receiving file data or connection closed.\n";
            break;
        }
        send(sock, "Received\n", 8, 0);
        if (strncmp(buffer, "EOFEOFEOFEOF", 12) == 0) {
            cout << "End of file reached.\n";
            break;
        }
        file.write(buffer, bytes);
        memset(buffer, 0, BUFFER_SIZE);
    }

    file.close();
    cout << "File downloaded successfully.\n";
}
