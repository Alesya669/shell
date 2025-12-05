#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

using namespace std;

// Global variable for signal handling
volatile sig_atomic_t running = true;

void signal_handler(int signum) {
    running = false;
}

// Check if a file exists
bool file_exists(const string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// Check if a directory exists
bool dir_exists(const string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) return false;
    return S_ISDIR(buffer.st_mode);
}

// Create directory recursively
bool create_directory(const string& path) {
    // Check if directory already exists
    if (dir_exists(path)) return true;
    
    // Create parent directories first
    size_t pos = path.find_last_of('/');
    if (pos != string::npos) {
        string parent = path.substr(0, pos);
        if (!parent.empty() && !create_directory(parent)) {
            return false;
        }
    }
    
    // Create the directory
    return mkdir(path.c_str(), 0755) == 0;
}

// Check if a command exists in PATH
string find_command(const string& cmd) {
    // Check if command contains a path
    if (cmd.find('/') != string::npos) {
        if (file_exists(cmd)) {
            return cmd;
        }
        return "";
    }
    
    // Search in PATH
    const char* path_env = getenv("PATH");
    if (!path_env) return "";
    
    stringstream ss(path_env);
    string path;
    
    while (getline(ss, path, ':')) {
        string full_path = path + "/" + cmd;
        if (file_exists(full_path)) {
            return full_path;
        }
    }
    
    return "";
}

// Execute a command
bool execute_command(const string& cmd) {
    vector<string> args;
    stringstream ss(cmd);
    string token;
    
    while (getline(ss, token, ' ')) {
        if (!token.empty()) {
            args.push_back(token);
        }
    }
    
    if (args.empty()) return false;
    
    // Check for built-in commands
    if (args[0] == "cat") {
        if (args.size() < 2) {
            cout << "cat: missing operand" << endl;
            return true;
        }
        
        ifstream file(args[1]);
        if (file) {
            string line;
            while (getline(file, line)) {
                cout << line << endl;
            }
            return true;
        } else {
            cout << "cat: " << args[1] << ": No such file or directory" << endl;
            return true;
        }
    }
    
    // Try to find and execute the command
    string command_path = find_command(args[0]);
    if (command_path.empty()) {
        // Check for VFS users directory
        if (args[0] == "ls" && args.size() == 2 && args[1] == "/opt/users") {
            // List VFS users
            DIR* dir = opendir("/opt/users");
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    if (entry->d_name[0] != '.') {
                        struct stat st;
                        string full_path = string("/opt/users/") + entry->d_name;
                        if (stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                            cout << entry->d_name << endl;
                        }
                    }
                }
                closedir(dir);
            }
            return true;
        }
        
        // Check for /etc/passwd reading
        if (args[0] == "cat" && args.size() > 1 && args[1] == "/etc/passwd") {
            ifstream passwd_file("/etc/passwd");
            if (passwd_file) {
                string line;
                while (getline(passwd_file, line)) {
                    cout << line << endl;
                }
            }
            return true;
        }
        
        return false;
    }
    
    // Prepare arguments for execv
    vector<char*> exec_args;
    for (auto& arg : args) {
        exec_args.push_back(const_cast<char*>(arg.c_str()));
    }
    exec_args.push_back(nullptr);
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execv(command_path.c_str(), exec_args.data());
        perror("execv failed");
        exit(1);
    } else if (pid > 0) {
        // Parent process
        waitpid(pid, nullptr, 0);
        return true;
    } else {
        perror("fork failed");
        return false;
    }
}

// Create user in VFS
void create_vfs_user(const string& username) {
    string user_dir = "/opt/users/" + username;
    create_directory(user_dir);
}

int main() 
{
    // Setup signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    vector<string> history;
    string input;
    string history_file = "kubsh_history.txt";
    ofstream write_file(history_file, ios::app);
    
    // Create /opt/users directory if it doesn't exist
    if (!dir_exists("/opt/users")) {
        create_directory("/opt/users");
    }
    
    while (running && getline(cin, input)) 
    {
        if (input.empty()) {
            continue;
        }
        
        write_file << input << endl;
        write_file.flush();
        
        // Check for exit command
        if (input == "\\q") 
        {
            running = false;
            break;
        }
        // Handle debug command
        else if (input.find("debug ") == 0) 
        {
            string text = input.substr(6);
            
            if (text.size() >= 2) 
            {
                char first = text[0];
                char last = text[text.size()-1];
                if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) 
                {
                    text = text.substr(1, text.size()-2);
                }
            }
            
            cout << text << endl;
            history.push_back(input);
        }
        // Handle environment variable listing
        else if (input.substr(0,4) == "\\e $") 
        {
            string var_name = input.substr(4);
            const char* env_value = getenv(var_name.c_str());
            
            if (env_value != nullptr) {
                string value(env_value);
                if (value.find(':') != string::npos) {
                    stringstream ss(value);
                    string item;
                    while (getline(ss, item, ':')) {
                        cout << item << endl;
                    }
                } else {
                    cout << value << endl;
                }
            } else {
                cout << "Environment variable '" << var_name << "' not found" << endl;
            }
        }
        // Handle commands starting with / (absolute path)
        else if (input[0] == '/')  
        {
            pid_t pid = fork();
            if (pid == 0) {
                // Child process
                vector<string> args;
                stringstream ss(input);
                string token;
                while (getline(ss, token, ' ')) {
                    args.push_back(token);
                }
                
                vector<char*> exec_args;
                for (auto& arg : args) {
                    exec_args.push_back(const_cast<char*>(arg.c_str()));
                }
                exec_args.push_back(nullptr);
                
                execv(exec_args[0], exec_args.data());
                perror("execv failed");
                exit(1);
            } else if (pid > 0) {
                waitpid(pid, nullptr, 0);
            } else {
                perror("fork failed");
            }
            history.push_back(input);
        }
        // Handle all other commands
        else 
        {
            if (!execute_command(input)) {
                cout << input << ": command not found" << endl;
            }
            history.push_back(input);
        }
        
        // Check if stdin is still good (for signal handling)
        if (!cin.good()) {
            running = false;
        }
    }
    
    write_file.close();
    return 0;
}
