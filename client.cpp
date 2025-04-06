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

#include <pwd.h>
#include <grp.h>
#include <ctime>
#include <sstream>
#include <vector>

#define BUFFER_SIZE 4096
#define RESET "\033[0m"
#define RED "\033[31m"     // Red text
#define GREEN "\033[32m"   // Green text
#define YELLOW "\033[33m"  // Yellow text
#define BLUE "\033[34m"    // Blue text
#define MAGENTA "\033[35m" // Magenta text
#define CYAN "\033[36m"    // Cyan text
#define BOLD "\033[1m"     // Bold text
#define LGREEN "\033[92m"
#define LBLUE "\033[94m"
using namespace std;

string send_command(int sock, const string &command);
void handle_put(int sock, const string &filename);
void handle_get(int sock, const string &filename);

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cerr << "Usage: " << argv[0] << " <server_ip> <port>\n";
        return EXIT_FAILURE;
    }

    const char *SERVER_IP = argv[1];
    int PORT = atoi(argv[2]);
    if (PORT <= 0 || PORT > 65535)
    {
        cerr << "Invalid port: " << argv[2] << "\n";
        return EXIT_FAILURE;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
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
    while (true)
    {

        server_dir = send_command(sock, "pwd");

        cout << LGREEN << "ftp: " << LBLUE << server_dir << "> " << RESET;
        string input;
        if (!getline(cin, input) || input.empty())
            continue;

        string command = input.substr(0, input.find(' '));
        string arg = (input.find(' ') != string::npos)
                         ? input.substr(input.find(' ') + 1)
                         : "";
        // cout << "Command: " << command << ", Arg: " << arg << endl;
        if (command == "lls")
        {
            char buffer[FILENAME_MAX];
            string client_directory;
            if (getcwd(buffer, sizeof(buffer)) != nullptr)
            {
                // cout << "Local working directory: " << buffer << std::endl;
                client_directory = buffer;
            }
            else
            {
                perror("getcwd error");
                return 1;
            }


            bool show_all   = false;
            bool long_fmt   = false;
            vector<string> tokens;
            

            istringstream iss(input);
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
                continue;
                // send(sock, err, strlen(err), 0);
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
                // send(sock, response.c_str(), response.size(), 0);
                cout << response << endl;
            }
        }
        else if (command == "lpwd")
        {
            char buffer[FILENAME_MAX];

            if (getcwd(buffer, sizeof(buffer)) != nullptr)
            {
                cout << "Local working directory: " << buffer << std::endl;
            }
            else
            {
                perror("getcwd error");
                return 1;
            }
        }
        else if (command == "lcd")
        {
            if (chdir(arg.c_str()) == 0)
                cout << "Directory changed\n";
            else
                cout << "Error changing directory\n";
        }
        else if (command == "pwd")
        {
            // ask the server for its current directory
            cout << send_command(sock, "pwd") << endl;
        }
        else if (command == "lchmod")
        {
            size_t space = arg.find(' ');
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
            // cout<<"hello"<<endl;
            string response = send_command(sock, input);
            // cout<<"hello"<<endl;
            cout << response << endl;
            // cout << "Unknown command: " << command << "\n";
        }
    }

    close(sock);
    return 0;
}

string send_command(int sock, const string &command)
{
    char buffer[BUFFER_SIZE];
    send(sock, command.c_str(), command.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    return buffer;
}

void handle_put(int sock, const string &filename)
{
    ifstream file(filename, ios::binary);
    if (!file)
    {
        cout << "File not found: " << filename << "\n";
        return;
    }

    char buffer[BUFFER_SIZE];
    string cmd = "put " + filename;
    send(sock, cmd.c_str(), cmd.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);

    if (strcmp(buffer, "Error opening file") == 0)
    {
        cout << buffer << "\n";
        return;
    }

    cout << "Sending...\n";
    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0)
    {
        int bytes_sent = send(sock, buffer, file.gcount(), 0);
        recv(sock, buffer, BUFFER_SIZE, 0);
        // cout << "Server Ack: " << buffer;
        if (bytes_sent <= 0)
        {
            cout << "Error sending file data\n";
            break;
        }
        memset(buffer, 0, BUFFER_SIZE);
    }

    send(sock, "EOFEOFEOFEOF\n", 12, 0);
    recv(sock, buffer, BUFFER_SIZE, 0);
    // cout << "Server Ack: " << buffer<<endl;
    if (strcmp(buffer, "Received") != 0)
        cout << "Error receiving acknowledgment\n";
    else
        cout << "File transfer complete.\n";
    file.close();
}

void handle_get(int sock, const string &filename)
{
    string cmd = "get " + filename;
    char buffer[BUFFER_SIZE];
    send(sock, cmd.c_str(), cmd.size(), 0);

    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    if (strcmp(buffer, "No such file exists") == 0)
    {
        cout << buffer << "\n";
        return;
    }

    ofstream file(filename, ios::binary);
    if (!file)
    {
        cout << "Error creating file: " << filename << "\n";
        return;
    }

    cout << "Receiving...\n";
    while (true)
    {
        int bytes = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes <= 0)
        {
            cout << "Error receiving file data or connection closed.\n";
            break;
        }
        send(sock, "Received\n", 8, 0);
        if (strncmp(buffer, "EOFEOFEOFEOF", 12) == 0)
        {
            cout << "End of file reached.\n";
            break;
        }
        file.write(buffer, bytes);
        memset(buffer, 0, BUFFER_SIZE);
    }

    file.close();
    cout << "File downloaded successfully.\n";
}
