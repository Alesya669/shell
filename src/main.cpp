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
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <cstring>

// Глобальные переменные для сигналов
volatile sig_atomic_t sighup_received = 0;
volatile sig_atomic_t running = true;

// Функция для обработки сигнала SIGHUP
void sighup_handler(int signal_number)
{
    if (signal_number == SIGHUP)
    {
        std::cout << "Configuration reloaded\n";
        std::cout << "$ ";
    }
}

// Обновленный обработчик SIGHUP
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

// Проверка дисковых разделов
void check_disk_partitions(const std::string& device_path)
{
    std::ifstream device(device_path, std::ios::binary);

    if (!device) 
    {
        std::cout << "Error: Cannot open device " << device_path << "\n";
        return;
    }
    
    char sector[512];
    device.read(sector, 512);
    
    if (device.gcount() != 512) 
    {
        std::cout << "Error: Cannot read disk\n";
        return;
    }
    
    if ((unsigned char)sector[510] != 0x55 || (unsigned char)sector[511] != 0xAA) 
    {
        std::cout << "Error: Invalid disk signature\n";
        return;
    }
    
    bool is_gpt = false;
    for (int i = 0; i < 4; i++) 
    {
        if ((unsigned char)sector[446 + i * 16 + 4] == 0xEE) 
        {
            is_gpt = true;
            break;
        }
    }
    
    if (!is_gpt) 
    {
        for (int i = 0; i < 4; i++) 
        {
            int offset = 446 + i * 16;
            unsigned char type = sector[offset + 4];
            
            if (type != 0) {
                uint32_t num_sectors = *(uint32_t*)&sector[offset + 12];
                uint32_t size_mb = num_sectors / 2048;
                bool bootable = ((unsigned char)sector[offset] == 0x80);
                
                std::cout << "Partition " << (i + 1) << ": Size=" << size_mb << "MB, Bootable: ";
                if(bootable)
                    std::cout << "Yes\n";
                else std::cout << "No\n";
            }
        }
    } 
    else 
    {
        device.read(sector, 512);
        if (device.gcount() == 512 && sector[0] == 'E' && sector[1] == 'F' && sector[2] == 'I' && sector[3] == ' ' && 
            sector[4] == 'P' && sector[5] == 'A' && sector[6] == 'R' && sector[7] == 'T') 
        {
            uint32_t num_partitions = *(uint32_t*)&sector[80];
            std::cout << "GPT partitions: " << num_partitions << "\n";
        } 
        else 
        {
            std::cout << "GPT partitions: unknown\n";
        }
    }
}

// Функция для выполнения команды
std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// Проверка существования файла
bool file_exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// Проверка существования директории
bool dir_exists(const std::string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) return false;
    return S_ISDIR(buffer.st_mode);
}

// Создание директории
bool create_directory(const std::string& path) {
    if (dir_exists(path)) return true;
    
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        std::string parent = path.substr(0, pos);
        if (!parent.empty()) {
            create_directory(parent);
        }
    }
    
    return mkdir(path.c_str(), 0755) == 0;
}

// Поиск команды в PATH
std::string find_in_path(const std::string& cmd) {
    if (cmd.find('/') != std::string::npos) {
        if (file_exists(cmd)) {
            return cmd;
        }
        return "";
    }
    
    const char* path_env = getenv("PATH");
    if (!path_env) return "";
    
    std::stringstream ss(path_env);
    std::string path;
    
    while (getline(ss, path, ':')) {
        if (path.empty()) continue;
        std::string full_path = path + "/" + cmd;
        if (file_exists(full_path)) {
            return full_path;
        }
    }
    
    return "";
}

// Выполнение внешней команды
bool execute_external(const std::vector<std::string>& args) {
    if (args.empty()) return false;
    
    std::string cmd_path = find_in_path(args[0]);
    if (cmd_path.empty()) return false;
    
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<char*> exec_args;
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

// VFS: Создание информации о пользователе
void create_user_vfs_info(const std::string& username) {
    std::string vfs_dir = "/opt/users";
    std::string user_dir = vfs_dir + "/" + username;
    
    if (!create_directory(user_dir)) {
        std::cerr << "Failed to create directory for user: " << username << std::endl;
        return;
    }
    
    struct passwd* pw = getpwnam(username.c_str());
    if (!pw) {
        std::ofstream id_file(user_dir + "/id");
        if (id_file) {
            id_file << "1000";
            id_file.close();
        }
        
        std::ofstream home_file(user_dir + "/home");
        if (home_file) {
            home_file << "/home/" + username;
            home_file.close();
        }
        
        std::ofstream shell_file(user_dir + "/shell");
        if (shell_file) {
            shell_file << "/bin/bash";
            shell_file.close();
        }
        
        std::string adduser_cmd = "sudo adduser --disabled-password --gecos '' " + username + " >/dev/null 2>&1";
        system(adduser_cmd.c_str());
    } else {
        std::ofstream id_file(user_dir + "/id");
        if (id_file) {
            id_file << pw->pw_uid;
            id_file.close();
        }
        
        std::ofstream home_file(user_dir + "/home");
        if (home_file) {
            home_file << pw->pw_dir;
            home_file.close();
        }
        
        std::ofstream shell_file(user_dir + "/shell");
        if (shell_file) {
            shell_file << pw->pw_shell;
            shell_file.close();
        }
    }
}

// VFS: Инициализация и синхронизация
void init_vfs() {
    std::string vfs_dir = "/opt/users";
    
    if (!create_directory(vfs_dir)) {
        std::cerr << "Failed to create VFS directory: " << vfs_dir << std::endl;
        return;
    }
    
    std::ifstream passwd_file("/etc/passwd");
    if (passwd_file) {
        std::string line;
        while (getline(passwd_file, line)) {
            if (line.find(":sh\n") != std::string::npos || line.find("/bin/bash") != std::string::npos) {
                std::vector<std::string> parts;
                std::stringstream ss(line);
                std::string part;
                
                while (getline(ss, part, ':')) {
                    parts.push_back(part);
                }
                
                if (parts.size() >= 7) {
                    std::string username = parts[0];
                    std::string shell = parts[6];
                    
                    if (shell == "/bin/bash" || shell == "/bin/sh") {
                        std::string user_dir = vfs_dir + "/" + username;
                        if (!dir_exists(user_dir)) {
                            create_directory(user_dir);
                            
                            std::ofstream id_file(user_dir + "/id");
                            if (id_file) {
                                id_file << parts[2];
                                id_file.close();
                            }
                            
                            std::ofstream home_file(user_dir + "/home");
                            if (home_file) {
                                home_file << parts[5];
                                home_file.close();
                            }
                            
                            std::ofstream shell_file(user_dir + "/shell");
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

// Функция для проверки и создания недостающих файлов в VFS
void check_and_create_vfs_files(const std::string& username) {
    std::string vfs_dir = "/opt/users";
    std::string user_dir = vfs_dir + "/" + username;
    
    if (!dir_exists(user_dir)) {
        return;
    }
    
    std::string id_file = user_dir + "/id";
    std::string home_file = user_dir + "/home";
    std::string shell_file = user_dir + "/shell";
    
    if (!file_exists(id_file) || !file_exists(home_file) || !file_exists(shell_file)) {
        struct passwd* pw = getpwnam(username.c_str());
        if (pw) {
            std::ofstream id_f(id_file);
            if (id_f) {
                id_f << pw->pw_uid;
                id_f.close();
            }
            
            std::ofstream home_f(home_file);
            if (home_f) {
                home_f << pw->pw_dir;
                home_f.close();
            }
            
            std::ofstream shell_f(shell_file);
            if (shell_f) {
                shell_f << pw->pw_shell;
                shell_f.close();
            }
        } else {
            std::ofstream id_f(id_file);
            if (id_f) {
                id_f << "1000";
                id_f.close();
            }
            
            std::ofstream home_f(home_file);
            if (home_f) {
                home_f << "/home/" + username;
                home_f.close();
            }
            
            std::ofstream shell_f(shell_file);
            if (shell_f) {
                shell_f << "/bin/bash";
                shell_f.close();
            }
            
            std::string adduser_cmd = "sudo adduser --disabled-password --gecos '' " + username + " >/dev/null 2>&1";
            system(adduser_cmd.c_str());
        }
    }
}

// VFS: Мониторинг изменений в директории
void monitor_vfs_changes() {
    std::string vfs_dir = "/opt/users";
    
    if (!dir_exists(vfs_dir)) {
        return;
    }
    
    DIR* dir = opendir(vfs_dir.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            std::string username = entry->d_name;
            std::string user_dir = vfs_dir + "/" + username;
            
            if (dir_exists(user_dir)) {
                check_and_create_vfs_files(username);
            }
        }
    }
    
    closedir(dir);
}

// Функция для удаления пользователя при удалении директории
void handle_user_deletion(const std::string& username) {
    std::string deluser_cmd = "sudo userdel -r " + username + " >/dev/null 2>&1";
    system(deluser_cmd.c_str());
}

int main() 
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    std::vector<std::string> history;
    std::string input;
    
    const char* home = std::getenv("HOME");
    std::string historyPath = std::string(home) + "/.kubsh_history";
    std::ofstream write_file(historyPath, std::ios::app);
    
    signal(SIGHUP, handle_sighup);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    init_vfs();
    
    while (running) 
    {
        monitor_vfs_changes();
        
        if (isatty(STDIN_FILENO)) {
            std::cout << "$ ";
        }
        std::cout.flush();
        
        if (!std::getline(std::cin, input)) {
            if (std::cin.eof()) {
                break;
            }
            continue;
        }
        
        if (input.empty()) {
            continue;
        }
        
        if (write_file.is_open()) {
            write_file << input << std::endl;
            write_file.flush();
        }
        history.push_back(input);
        
        if (input == "history")
        {
            std::ifstream historyOutput(historyPath);
            std::string line;
            while(std::getline(historyOutput, line))
            {
                std::cout << line << "\n";
            }
        }
        else if (input == "\\q")
        {
            running = false;
            break;
        }
        else if (input.substr(0, 3) == "\\l ") 
        {
            std::string device_path = input.substr(3);
            device_path.erase(0, device_path.find_first_not_of(" \t"));
            device_path.erase(device_path.find_last_not_of(" \t") + 1);
            
            if (device_path.empty()) 
            {
                std::cout << "Usage: \\l /dev/device_name (e.g., \\l /dev/sda)\n";
            } 
            else 
            {
                check_disk_partitions(device_path);
            }
        }
        else if (input.substr(0, 7) == "debug '" && input[input.length() - 1] == '\'')
        {
            std::cout << input.substr(7, input.length() - 8) << std::endl;  
            continue;
        }
        else if (input.substr(0,4) == "\\e $")
        {
            std::string varName = input.substr(4);
            const char* value = std::getenv(varName.c_str());

            if(value != nullptr)
            {
                std::string valueStr = value;
                
                bool has_colon = false;
                for (char c : valueStr)
                {
                    if (c == ':') 
                    {
                        has_colon = true;
                        break;
                    }
                }
                
                if (has_colon) 
                {
                    std::string current_part = "";
                    for (char c : valueStr)
                    {
                        if (c == ':') 
                        {
                            std::cout << current_part << "\n";
                            current_part = "";
                        }
                        else 
                        {
                            current_part += c;
                        }
                    }
                    std::cout << current_part << "\n";
                }
                else 
                { 
                    std::cout << valueStr << "\n";
                }
            }
            else
            {
                std::cout << varName << ": не найдено\n";
            }
            continue;
        }
        else 
        {
            pid_t pid = fork();
            
            if (pid == 0) 
            {
                std::vector<std::string> tokens;
                std::vector<char*> args;
                std::string token;
                std::istringstream iss(input);
                
                while (iss >> token) 
                {
                    tokens.push_back(token);
                }
                
                for (auto& t : tokens) 
                {
                    args.push_back(const_cast<char*>(t.c_str()));
                }
                args.push_back(nullptr);
                
                execvp(args[0], args.data());
                
                std::cout << args[0] << ": command not found\n";
                exit(1);
                
            } 
            else if (pid > 0) 
            {
                int status;
                waitpid(pid, &status, 0);
            } 
            else 
            {
                std::cerr << "Failed to create process\n";
            }
        }
        
        std::cout << "$ ";
    }
    
    if (write_file.is_open()) {
        write_file.close();
    }
    
    return 0;
}
