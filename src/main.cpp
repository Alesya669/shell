#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <sstream>
#include <signal.h>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <array>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <cstring>

#include "vfs.h"

using namespace std;

// Глобальные переменные для сигналов
volatile sig_atomic_t sighup_received = 0;
volatile sig_atomic_t running = true;

// Обработчик SIGHUP
void handle_sighup(int signum) {
    (void)signum;
    const char* msg = "Configuration reloaded\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    sighup_received = 1;
}

// Обработчик других сигналов
void handle_signal(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        running = false;
    }
}

// Проверка существования файла
bool file_exists(const string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// Проверка существования директории
bool dir_exists(const string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) return false;
    return S_ISDIR(buffer.st_mode);
}

// Создание директории
bool create_directory(const string& path) {
    if (dir_exists(path)) return true;
    
    size_t pos = path.find_last_of('/');
    if (pos != string::npos) {
        string parent = path.substr(0, pos);
        if (!parent.empty()) {
            create_directory(parent);
        }
    }
    
    return mkdir(path.c_str(), 0755) == 0;
}

// Поиск команды в PATH
string find_in_path(const string& cmd) {
    if (cmd.find('/') != string::npos) {
        if (file_exists(cmd)) {
            return cmd;
        }
        return "";
    }
    
    const char* path_env = getenv("PATH");
    if (!path_env) return "";
    
    stringstream ss(path_env);
    string path;
    
    while (getline(ss, path, ':')) {
        if (path.empty()) continue;
        string full_path = path + "/" + cmd;
        if (file_exists(full_path)) {
            return full_path;
        }
    }
    
    return "";
}

// Выполнение внешней команды
bool execute_external(const vector<string>& args) {
    if (args.empty()) return false;
    
    string cmd_path = find_in_path(args[0]);
    if (cmd_path.empty()) return false;
    
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        vector<char*> exec_args;
        for (const auto& arg : args) {
            exec_args.push_back(const_cast<char*>(arg.c_str()));
        }
        exec_args.push_back(nullptr);
        
        execv(cmd_path.c_str(), exec_args.data());
        exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return true;
    }
    
    return false;
}

void check_disk_partitions(const string& device_path) {
    ifstream device(device_path, ios::binary);

    if (!device) {
        cout << "Error: Cannot open device " << device_path << "\n";
        return;
    }
    
    char sector[512];
    device.read(sector, 512);
    
    if (device.gcount() != 512) {
        cout << "Error: Cannot read disk\n";
        return;
    }
    
    if ((unsigned char)sector[510] != 0x55 || (unsigned char)sector[511] != 0xAA) {
        cout << "Error: Invalid disk signature\n";
        return;
    }
    
    bool is_gpt = false;
    for (int i = 0; i < 4; i++) {
        if ((unsigned char)sector[446 + i * 16 + 4] == 0xEE) {
            is_gpt = true;
            break;
        }
    }
    
    if (!is_gpt) {
        for (int i = 0; i < 4; i++) {
            int offset = 446 + i * 16;
            unsigned char type = sector[offset + 4];
            
            if (type != 0) {
                uint32_t num_sectors = *(uint32_t*)&sector[offset + 12];
                uint32_t size_mb = num_sectors / 2048;
                bool bootable = ((unsigned char)sector[offset] == 0x80);
                
                cout << "Partition " << (i + 1) << ": Size=" << size_mb << "MB, Bootable: ";
                if(bootable)
                    cout<<"Yes\n";
                else cout<<"No\n";
            }
        }
    } else {
        device.read(sector, 512);
        if (device.gcount() == 512 && sector[0] == 'E' && sector[1] == 'F' && sector[2] == 'I' && sector[3] == ' ' && 
            sector[4] == 'P' && sector[5] == 'A' && sector[6] == 'R' && sector[7] == 'T') {
            uint32_t num_partitions = *(uint32_t*)&sector[80];
            cout << "GPT partitions: " << num_partitions << "\n";
        } else {
            cout << "GPT partitions: unknown\n";
        }
    }
}

int main() {
    cout << unitbuf;
    cerr << unitbuf;
    
    fuse_start();

    vector<string> history;
    string input;
    
    const char* home = getenv("HOME");
    string history_file = string(home) + "/.kubsh_history";
    ofstream history_out(history_file, ios::app);
    
    // Устанавливаем обработчики сигналов
    signal(SIGHUP, handle_sighup);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    while (running) {
        if (isatty(STDIN_FILENO)) {
            cout << "kubsh> ";
        }
        cout.flush();
        
        if (!getline(cin, input)) {
            if (cin.eof()) break;
            continue;
        }
        
        if (input.empty()) continue;
        
        // Сохраняем в историю
        if (history_out.is_open()) {
            history_out << input << endl;
            history_out.flush();
        }
        history.push_back(input);
        
        if (input == "history") {
            ifstream history_in(history_file);
            string line;
            while (getline(history_in, line)) {
                cout << line << "\n";
            }
        }
        else if (input == "\\q") {
            break;
        }
        else if (input.substr(0, 3) == "\\l ") {
            string device_path = input.substr(3);
            device_path.erase(0, device_path.find_first_not_of(" \t"));
            device_path.erase(device_path.find_last_not_of(" \t") + 1);
            
            if (device_path.empty()) {
                cout << "Usage: \\l /dev/device_name (e.g., \\l /dev/sda)\n";
            } else {
                check_disk_partitions(device_path);
            }
        }
        else if (input.substr(0, 7) == "debug '" && input[input.length() - 1] == '\'') {
            cout << input.substr(7, input.length() - 8) << endl;
        }
        else if (input.substr(0,4) == "\\e $") {
            string varName = input.substr(4);
            const char* value = getenv(varName.c_str());
            
            if(value != nullptr) {
                string valueStr = value;
                bool has_colon = false;
                for (char c : valueStr) {
                    if (c == ':') {
                        has_colon = true;
                        break;
                    }
                }
                
                if (has_colon) {
                    string current_part = "";
                    for (char c : valueStr) {
                        if (c == ':') {
                            cout << current_part << "\n";
                            current_part = "";
                        } else {
                            current_part += c;
                        }
                    }
                    cout << current_part << "\n";
                } else {
                    cout << valueStr << "\n";
                }
            } else {
                cout << varName << ": не найдено\n";
            }
        }
        else {
            // Разбиваем ввод на аргументы
            vector<string> args;
            stringstream ss(input);
            string token;
            while (ss >> token) {
                args.push_back(token);
            }
            
            if (args.empty()) continue;
            
            // Команды для работы с VFS
            if (args[0] == "mkdir" && args.size() > 1) {
                string dir_path = args[1];
                create_directory(dir_path);
            }
            else if (args[0] == "ls" && args.size() > 1 && args[1] == "/opt/users") {
                if (dir_exists("/opt/users")) {
                    DIR* dir = opendir("/opt/users");
                    if (dir) {
                        struct dirent* entry;
                        while ((entry = readdir(dir)) != nullptr) {
                            if (entry->d_name[0] != '.') {
                                string full_path = string("/opt/users/") + entry->d_name;
                                if (dir_exists(full_path)) {
                                    cout << entry->d_name << endl;
                                }
                            }
                        }
                        closedir(dir);
                    }
                } else {
                    cout << "ls: cannot access '/opt/users': No such file or directory" << endl;
                }
            }
            else if (args[0] == "rmdir" && args.size() > 1) {
                rmdir(args[1].c_str());
            }
            else if (args[0] == "cat" && args.size() > 1 && args[1] == "/etc/passwd") {
                ifstream file("/etc/passwd");
                if (file) {
                    string line;
                    while (getline(file, line)) {
                        cout << line << endl;
                    }
                    file.close();
                } else {
                    cout << "cat: /etc/passwd: No such file or directory" << endl;
                }
            }
            else {
                // Выполнение внешней команды
                if (!execute_external(args)) {
                    cout << args[0] << ": command not found" << endl;
                }
            }
        }
        
        cout.flush();
    }
    
    if (history_out.is_open()) {
        history_out.close();
    }
    
    return 0;
}
