#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <cstring>
#include <cstdint>

#include "vfs.hpp"

using namespace std;

// ==================== Глобальные переменные ====================
volatile sig_atomic_t sighup_received = 0;
volatile sig_atomic_t running = true;

// ==================== Функции для работы с сигналами ====================
void handle_sighup(int signum) {
    (void)signum;
    const char* msg = "Configuration reloaded\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    sighup_received = 1;
}

void handle_signal(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        running = false;
    }
}

// ==================== Вспомогательные функции ====================
bool file_exists(const string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool dir_exists(const string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) return false;
    return S_ISDIR(buffer.st_mode);
}

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

string exec(const char* cmd) {
    array<char, 128> buffer;
    string result;
    unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// ==================== Функции для работы с дисками ====================
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
                cout << (bootable ? "Yes\n" : "No\n");
            }
        }
    } else {
        device.read(sector, 512);
        if (device.gcount() == 512 && 
            sector[0] == 'E' && sector[1] == 'F' && sector[2] == 'I' && sector[3] == ' ' && 
            sector[4] == 'P' && sector[5] == 'A' && sector[6] == 'R' && sector[7] == 'T') {
            uint32_t num_partitions = *(uint32_t*)&sector[80];
            cout << "GPT partitions: " << num_partitions << "\n";
        } else {
            cout << "GPT partitions: unknown\n";
        }
    }
}

// ==================== Функции для выполнения команд ====================
bool execute_external(const vector<string>& args) {
    if (args.empty()) return false;
    
    string cmd_path = find_in_path(args[0]);
    if (cmd_path.empty()) return false;
    
    pid_t pid = fork();
    if (pid == 0) {
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

void execute_external_legacy(const string& input) {
    pid_t pid = fork();
    
    if (pid == 0) {
        vector<string> tokens;
        vector<char*> args;
        string token;
        istringstream iss(input);
        
        while (iss >> token) {
            tokens.push_back(token);
        }
        
        for (auto& t : tokens) {
            args.push_back(const_cast<char*>(t.c_str()));
        }
        args.push_back(nullptr);
        
        execvp(args[0], args.data());
        cout << args[0] << ": command not found\n";
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    } else {
        cerr << "Failed to create process\n";
    }
}

// ==================== Функции для работы с VFS ====================
void create_user_vfs_info(const string& username) {
    string vfs_dir = "/opt/users";
    string user_dir = vfs_dir + "/" + username;
    
    if (!create_directory(user_dir)) {
        cerr << "Failed to create directory for user: " << username << endl;
        return;
    }
    
    struct passwd* pw = getpwnam(username.c_str());
    if (!pw) {
        ofstream id_file(user_dir + "/id");
        if (id_file) {
            id_file << "1000" << endl;
            id_file.close();
        }
        
        ofstream home_file(user_dir + "/home");
        if (home_file) {
            home_file << "/home/" + username << endl;
            home_file.close();
        }
        
        ofstream shell_file(user_dir + "/shell");
        if (shell_file) {
            shell_file << "/bin/bash" << endl;
            shell_file.close();
        }
        
        string adduser_cmd = "sudo adduser --disabled-password --gecos '' " + username + " >/dev/null 2>&1";
        system(adduser_cmd.c_str());
    } else {
        ofstream id_file(user_dir + "/id");
        if (id_file) {
            id_file << pw->pw_uid << endl;
            id_file.close();
        }
        
        ofstream home_file(user_dir + "/home");
        if (home_file) {
            home_file << pw->pw_dir << endl;
            home_file.close();
        }
        
        ofstream shell_file(user_dir + "/shell");
        if (shell_file) {
            shell_file << pw->pw_shell << endl;
            shell_file.close();
        }
    }
}

void init_vfs() {
    string vfs_dir = "/opt/users";
    
    if (!create_directory(vfs_dir)) {
        cerr << "Failed to create VFS directory: " << vfs_dir << endl;
        return;
    }
    
    ifstream passwd_file("/etc/passwd");
    if (passwd_file) {
        string line;
        while (getline(passwd_file, line)) {
            if (line.find("/bin/bash") != string::npos || line.find("/bin/sh") != string::npos) {
                vector<string> parts;
                stringstream ss(line);
                string part;
                
                while (getline(ss, part, ':')) {
                    parts.push_back(part);
                }
                
                if (parts.size() >= 7) {
                    string username = parts[0];
                    string shell = parts[6];
                    
                    if (shell == "/bin/bash" || shell == "/bin/sh") {
                        string user_dir = vfs_dir + "/" + username;
                        if (!dir_exists(user_dir)) {
                            create_directory(user_dir);
                            
                            ofstream id_file(user_dir + "/id");
                            if (id_file) {
                                id_file << parts[2];
                                id_file.close();
                            }
                            
                            ofstream home_file(user_dir + "/home");
                            if (home_file) {
                                home_file << parts[5];
                                home_file.close();
                            }
                            
                            ofstream shell_file(user_dir + "/shell");
                            if (shell_file) {
                                shell_file << shell;
                                shell_file.close();
                            }
                        }
                    }
                }
            }
        }
        passwd_file.close();
    }
}

void handle_user_deletion(const string& username) {
    string deluser_cmd = "sudo userdel -r " + username + " >/dev/null 2>&1";
    system(deluser_cmd.c_str());
}

// ==================== Обработка встроенных команд ====================
void process_history(const string& history_file) {
    ifstream history_in(history_file);
    string line;
    while (getline(history_in, line)) {
        cout << line << "\n";
    }
}

void process_debug(const string& input) {
    cout << input.substr(7, input.length() - 8) << endl;
}

void process_echo(const string& input) {
    if (input.substr(0, 7) == "debug '" && input[input.length() - 1] == '\'') {
        process_debug(input);
        return;
    }
    
    string result;
    for (size_t i = 1; i < input.length(); ++i) {
        result += input[i];
    }
    
    if (result.size() >= 2) {
        char first = result[0];
        char last = result[result.size()-1];
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            result = result.substr(1, result.size()-2);
        }
    }
    
    cout << result << endl;
}

void process_env_var(const string& varName) {
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

void process_disk_info(const string& device_path) {
    string trimmed_path = device_path;
    trimmed_path.erase(0, trimmed_path.find_first_not_of(" \t"));
    trimmed_path.erase(trimmed_path.find_last_not_of(" \t") + 1);
    
    if (trimmed_path.empty()) {
        cout << "Usage: \\l /dev/device_name (e.g., \\l /dev/sda)\n";
    } else {
        check_disk_partitions(trimmed_path);
    }
}

// ==================== Основная функция ====================
int main() {
    cout << unitbuf;
    cerr << unitbuf;
    
    // Запуск FUSE
    fuse_start();
    
    vector<string> history;
    string input;
    
    const char* home = getenv("HOME");
    string history_file = string(home) + "/.kubsh_history";
    ofstream history_out(history_file, ios::app);
    
    // Установка обработчиков сигналов
    signal(SIGHUP, handle_sighup);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Инициализация VFS
    init_vfs();
    
    // Основной цикл
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
        
        // Сохранение в историю
        if (history_out.is_open()) {
            history_out << input << endl;
            history_out.flush();
        }
        history.push_back(input);
        
        // Обработка специальных команд
        if (input == "history") {
            process_history(history_file);
        }
        else if (input == "\\q") {
            break;
        }
        else if (input.substr(0, 3) == "\\l ") {
            process_disk_info(input.substr(3));
        }
        else if (input.substr(0, 7) == "debug '" && input[input.length() - 1] == '\'') {
            process_debug(input);
        }
        else if (input.substr(0,4) == "\\e $") {
            process_env_var(input.substr(4));
        }
        else if (input.substr(0, 5) == "echo ") {
            process_echo(input);
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
            
            // Обработка команд управления файлами
            if (args[0] == "cat" && args.size() > 1 && args[1] == "/etc/passwd") {
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
            else if (args[0] == "mkdir" && args.size() > 1) {
                string dir_path = args[1];
                if (dir_path.find("/opt/users/") == 0) {
                    string username = dir_path.substr(strlen("/opt/users/"));
                    if (!username.empty() && username.find('/') == string::npos) {
                        create_user_vfs_info(username);
                        cout << "Created VFS directory for user: " << username << endl;
                    } else {
                        create_directory(dir_path);
                    }
                } else {
                    create_directory(dir_path);
                }
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
                string dir_path = args[1];
                if (dir_path.find("/opt/users/") == 0) {
                    string username = dir_path.substr(strlen("/opt/users/"));
                    if (!username.empty() && username.find('/') == string::npos) {
                        handle_user_deletion(username);
                        string cmd = "rm -rf \"" + dir_path + "\"";
                        system(cmd.c_str());
                        cout << "Removed VFS directory and user: " << username << endl;
                    } else {
                        rmdir(dir_path.c_str());
                    }
                } else {
                    rmdir(dir_path.c_str());
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
