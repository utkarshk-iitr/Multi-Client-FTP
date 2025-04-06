#include <iostream>
#include <pwd.h>
#include <grp.h>
#include <ctime>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <dirent.h>
#include <vector>
#include <sys/stat.h>

#define MAX_CLIENTS 8
#define BUFFER_SIZE 4096
#define RESET   "\033[0m"
#define RED     "\033[31m"      // Red text
#define GREEN   "\033[32m"      // Green text
#define YELLOW  "\033[33m"      // Yellow text
#define BLUE    "\033[34m"      // Blue text
#define MAGENTA "\033[35m"      // Magenta text
#define CYAN    "\033[36m"      // Cyan text
#define BOLD    "\033[1m"       // Bold text

using namespace std;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
void *handle_client(void *client_socket);

int main(int argc, char *argv[])
{
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <port>\n";
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        cerr << "Invalid port number: " << argv[1] << "\n";
        return EXIT_FAILURE;
    }

    int server_fd, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("Cant reuse");
        exit(EXIT_FAILURE);
    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    cout << "FTP Server started on port " << port << "...\n";

    while (true) {
        client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0) {
            perror("Client accept failed");
            continue;
        }
        pthread_t thread_id;
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
    string client_directory = "."; // Each client starts in the server root
    cout<<"Client connected.\n"<<endl;

    while (true)
    {
        memset(buffer, 0, BUFFER_SIZE);
        if (recv(sock, buffer, BUFFER_SIZE, 0) <= 0)
        {
            cout << "Client disconnected.\n";
            close(sock);
            pthread_exit(NULL);
        }

        string command(buffer);
        command = command.substr(0, command.find("\n")); // Remove newline

        if (command == "pwd")
        {
            // send the client's current working directory
            send(sock, client_directory.c_str(), client_directory.size(), 0);
        }
        
        else if (command.substr(0, 2) == "ls")
        {
            // 1) parse options
            bool show_all   = false;
            bool long_fmt   = false;
            vector<string> tokens;
            

            istringstream iss(command);
            string t;
            while (iss >> t) tokens.push_back(t);
            
            int f=0;
            for (size_t i = 1; i < tokens.size(); ++i)
            {
                // cout<<"tokens[i]="<<tokens[i]<<endl;
                string opt = tokens[i];
                if (opt == "-a")          
                {
                    show_all = true;
                    // cout<<"here"<<endl;
                }
                else if (opt == "-l")     
                {
                    long_fmt = true;
                    // cout<<"here2"<<endl;
                }
                else if (opt == "-al" || opt == "-la")   
                { 
                    show_all = long_fmt = true; 
                    // cout<<"here3"<<endl;
                }
                else
                {
                    // Invalid option
                    // cout<<"here4"<<endl;
                    const char *err = "Invalid option\n";
                    send(sock, err, strlen(err), 0);
                    f=-1;
                    break;
                }
            }
            
            if(f==-1)
            {
                continue;
            }
            // 2) open directory
            DIR *dir = opendir(client_directory.c_str());
            if (!dir)
            {
                const char *err = "Error opening directory\n";
                send(sock, err, strlen(err), 0);
            }
            else
            {
                struct dirent *entry;
                string response;
                while ((entry = readdir(dir)) != nullptr)
                {
                    const char *name = entry->d_name;
                    // skip hidden unless -a
                    if (!show_all && name[0] == '.') 
                        continue;
        
                    if (!long_fmt)
                    {
                        response += name;
                        response += '\n';
                    }
                    else
                    {
                        // build full path and stat it
                        string full = client_directory + "/" + name;
                        struct stat st;
                        if (stat(full.c_str(), &st) != 0) 
                            continue;  // skip if stat fails
        
                        // permissions
                        char perms[11] = {'-','r','w','x','r','w','x','r','w','x','\0'};
                        perms[0] = S_ISDIR(st.st_mode) ? 'd' : '-';
                        perms[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
                        perms[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
                        perms[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
                        perms[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
                        perms[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
                        perms[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
                        perms[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
                        perms[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
                        perms[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
        
                        // link count
                        string links = to_string(st.st_nlink);
        
                        // owner / group
                        struct passwd *pw = getpwuid(st.st_uid);
                        struct group  *gr = getgrgid(st.st_gid);
                        string owner = pw ? pw->pw_name : to_string(st.st_uid);
                        string group = gr ? gr->gr_name : to_string(st.st_gid);
        
                        // size
                        string size = to_string(st.st_size);
        
                        // mtime
                        char timebuf[64];
                        struct tm *tm = localtime(&st.st_mtime);
                        strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm);
        
                        // assemble line
                        response += perms; response += ' ';
                        response += links; response += ' ';
                        response += owner; response += ' ';
                        response += group; response += ' ';
                        response += size;  response += ' ';
                        response += timebuf; response += ' ';
                        response += name;  response += '\n';
                    }
                }
                closedir(dir);
        
                // send it all at once
                send(sock, response.c_str(), response.size(), 0);
            }
        }

        else if (command.substr(0, 2) == "cd")
        {
            string path = command.substr(3);
            string candidate = client_directory + "/" + path;

            char resolved[PATH_MAX];
            if (realpath(candidate.c_str(), resolved) == nullptr) 
            {
                // realpath failed (e.g. path doesn't exist)
                send(sock, "Error changing directory\n", 25, 0);
            } 
            else 
            {
                struct stat st;
                if (stat(resolved, &st) == 0 && S_ISDIR(st.st_mode)) 
                {
                    // Optionally: enforce chroot‑style restriction so clients
                    // can't escape some “root” directory:
                    // if (std::string(resolved).rfind(server_root, 0) != 0) { …reject… }

                    client_directory = resolved;
                    send(sock, "Directory changed\n", 18, 0);
                } else 
                {
                    send(sock, "Error changing directory\n", 25, 0);
                }
            }
            // cout << "Current directory: " << client_directory << endl;
        }

        else if (command.substr(0, 5) == "chmod")
        {
            string args = command.substr(6);
            size_t space = args.find(" ");
            if (space == string::npos)
            {
                send(sock, "Invalid chmod command\n", 22, 0);
                continue;
            }

            string mode = args.substr(0, space);
            string filename = args.substr(space + 1);
            // Update to use the client's current directory.
            string filepath = client_directory + "/" + filename;

            if (chmod(filepath.c_str(), stoi(mode, nullptr, 8)) == 0)
                send(sock, "Permissions changed\n", 20, 0);
            else
                send(sock, "Error changing permissions\n", 27, 0);
        }


        else if (command.substr(0, 3) == "put")
        {
            pthread_mutex_lock(&file_mutex);
            string filename = command.substr(4);
            string filepath = client_directory + "/" + filename;

            ofstream file(filepath, ios::binary);
            if (!file)
            {
                send(sock, "Error opening file\n", 19, 0);
                pthread_mutex_unlock(&file_mutex);
                continue;
            }

            send(sock, "File opened\n", 12, 0);

            // Receive file content
            int total_received = 0;
            cout<<"Receiving..."<<endl;
            while (true)
            {
                int bytes = recv(sock, buffer, BUFFER_SIZE, 0);
                if (bytes <= 0)
                {
                    cout << "Error receiving file data or connection closed.\n";
                    pthread_mutex_unlock(&file_mutex);
                    break;
                }

                // cout<<"b="<<buffer<<endl;
                // cout<<buffer<<endl;
                send(sock, "Received\n", 8, 0); // Acknowledge receipt of data
                string data(buffer, bytes);
                                
                if(strcmp(buffer, "EOFEOFEOFEOF") == 0){
                    pthread_mutex_unlock(&file_mutex);
                    break;
                }
                else if (bytes == 0)
                {
                    pthread_mutex_unlock(&file_mutex);
                    break;
                }

                file.write(buffer, bytes);
                total_received += bytes;
                // cout << "Received: " << total_received << " bytes\n";
                memset(buffer, 0, BUFFER_SIZE);
            }

            file.close();
            cout << "File uploaded successfully.\n\n";
            pthread_mutex_unlock(&file_mutex);
        }

        else if (command.substr(0, 3) == "get")
        {
            pthread_mutex_lock(&file_mutex);
            string filename = command.substr(4);
            string filepath = client_directory + "/" + filename;

            ifstream file(filepath, ios::binary);
            if (!file)
            {
                send(sock, "No such file exists\n", 19, 0);
                pthread_mutex_unlock(&file_mutex);
                continue;
            }

            send(sock, "File opened\n", 12, 0);

            char buffer[BUFFER_SIZE];
            char buffer2[BUFFER_SIZE];
            cout<<"Sending..."<<endl;
            while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0)
            {
                int bytes_sent = send(sock, buffer, file.gcount(), 0);

                // cout<<buffer<<endl;
                recv(sock, buffer2, BUFFER_SIZE, 0);
                cout<<"Client Acknowledgement: "<<buffer2<<endl;

                if(strcmp(buffer2, "Received") != 0)
                {
                    cout << "Error receiving acknowledgment\n" << endl;
                }
                
                // cout<<"buffer="<<buffer2<<endl;
                if (bytes_sent <= 0)
                {
                    cout << "Error sending file data\n";
                    break;
                }
                memset(buffer2, 0, BUFFER_SIZE);
                memset(buffer, 0, BUFFER_SIZE);
            }
            // cout<<"sending eof"<<endl;
            send(sock, "EOFEOFEOFEOF\n", 12, 0);

            recv(sock, buffer2, BUFFER_SIZE, 0);
            if(strcmp(buffer2, "Received") != 0)
            {
                cout << "Error receiving acknowledgment\n" << endl;
            }
            
            cout << "File transfer complete.\n";

            file.close();
            pthread_mutex_unlock(&file_mutex);
        }

        else if (command == "close")
        {
            cout<<"One connection closed"<<endl;
            send(sock, "Closing connection...\n", 22, 0);
            close(sock);
            pthread_exit(NULL);
        }

        else
        {
            cout<<"Invalid="<<command<<endl;
            send(sock, "Invalid command\n", 16, 0);
        }
    }
}
