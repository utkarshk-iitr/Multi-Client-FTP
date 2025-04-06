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

#define SERVER_IP "192.168.0.237"
#define PORT 8080
#define BUFFER_SIZE 4096

using namespace std;

void send_command(int sock, string command);
void handle_put(int sock, string filename);
void handle_get(int sock, string filename);

int main()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    cout << "Connected to FTP server at " << SERVER_IP << ":" << PORT << endl;

    while (true)
    {
        cout << "ftp> ";
        string input;
        getline(cin, input);
        if (input.empty())
            continue;

        string command = input.substr(0, input.find(" "));
        string arg = (input.find(" ") != string::npos) ? input.substr(input.find(" ") + 1) : "";

        if (command == "lls")
        {
            DIR *dir;
            struct dirent *entry;
            if ((dir = opendir(".")) != NULL)
            {
                while ((entry = readdir(dir)) != NULL)
                {
                    cout << entry->d_name << endl;
                }
                closedir(dir);
            }
            else
            {
                cout << "Error listing directory\n";
            }
        }

        else if (command == "lcd")
        {
            if (chdir(arg.c_str()) == 0)
                cout << "Directory changed\n";
            else
                cout << "Error changing directory\n";
        }

        else if (command == "lchmod")
        {
            size_t space = arg.find(" ");
            if (space == string::npos)
            {
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

        else if (command == "put")
        {
            handle_put(sock, arg);
        }

        else if (command == "get")
        {
            handle_get(sock, arg);
        }

        else if (command == "close")
        {
            send_command(sock, "close");
            cout << "Closing connection...\n";
            break;
        }

        else
        {
            send_command(sock, input);
        }
    }

    close(sock);
    return 0;
}

void send_command(int sock, string command)
{
    char buffer[BUFFER_SIZE];
    send(sock, command.c_str(), command.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    cout << buffer;
}

void handle_put(int sock, string filename)
{
    ifstream file(filename, ios::binary);
    if (!file)
    {
        cout << "File not found: " << filename << endl;
        return;
    }

    send_command(sock, "put " + filename);
    // Get file size
    file.seekg(0, ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, ios::beg);

    // Send file size first
    if (send(sock, &file_size, sizeof(file_size), 0) <= 0)
    {
        cout << "Error sending file size\n";
        file.close();
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t total_sent = 0;

    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0)
    {
        int bytes_sent = send(sock, buffer, file.gcount(), 0);
        if (bytes_sent <= 0)
        {
            cout << "Error sending file data\n";
            break;
        }
        total_sent += bytes_sent;
    }

    file.close();
    cout << "File upload complete: " << total_sent << " bytes sent.\n";
}

void handle_get(int sock, string filename)
{
    string command = "get " + filename;
    char buffer[BUFFER_SIZE];
    send(sock, command.c_str(), command.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);

    cout << buffer<< endl;
    memset(buffer, 0, BUFFER_SIZE);

    if (strcmp(buffer, "No such file exists") == 0)
    {
        cout << buffer << endl;
        return;
    }

    ofstream file(filename, ios::binary);
    if (!file)
    {
        cout << "Error creating file: " << filename << endl;
        return;
    }

    memset(buffer, 0, BUFFER_SIZE);
    int total_received = 0;
    while (true)
    {
        int bytes = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes <= 0)
        {
            cout << "Error receiving file data or connection closed.\n";
            break;
        }
        
        cout<<buffer<<endl;
        send(sock, "Received\n", 8, 0); // Acknowledge receipt of data
        string data(buffer, bytes);
        

        
        if(strcmp(buffer, "EOFEOFEOFEOF") == 0 || total_received >= 1000)
        {
            cout << "End of file reached.\n" <<endl;
            break;
        }
        else if (bytes == 0)
        {
            cout << "End of file reached.\n" <<endl;
            break;
        }

        file.write(buffer, bytes);
        total_received += bytes;
        cout << "Received: " << total_received << " bytes\n";
        memset(buffer, 0, BUFFER_SIZE);
    }

    file.close();
    cout << "File downloaded successfully.\n";
}

