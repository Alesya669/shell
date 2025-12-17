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

using namespace std;

// Глобальные переменные для сигналов
volatile sig_atomic_t sighup_received = 0;
volatile sig_atomic_t running = true;

// Обновленный обработчик SIGHUP - выводит прямо в stdout
void handle_sighup(int signum) {
    (void)signum;
    // Выводим напрямую в stdout для гарантии
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

// Функция для выполнения команды
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

// VFS: Создание информации о пользователе
void create_user_vfs_info(const string& username) {
    string vfs_dir = "/opt/users";
    string user_dir = vfs_dir + "/" + username;
    
    // Создаем директорию пользователя
    if (!create_directory(user_dir)) {
        cerr << "Failed to create directory for user: " << username << endl;
        return;
    }
    
    // Получаем информацию о пользователе
    struct passwd* pw = getpwnam(username.c_str());
    if (!pw) {
        // Пользователь не существует в системе - создаем временные файлы
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
        
        // Пытаемся добавить пользователя в систему (с sudo)
        string adduser_cmd = "sudo adduser --disabled-password --gecos '' " + username + " >/dev/null 2>&1";
        system(adduser_cmd.c_str());
    } else {
        // Пользователь существует - создаем файлы с реальными данными
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

// VFS: Инициализация и синхронизация (ТОЛЬКО для пользователей с /bin/bash или /bin/sh)
void init_vfs() {
    string vfs_dir = "/opt/users";
    
    // Создаем основную директорию
    if (!create_directory(vfs_dir)) {
        cerr << "Failed to create VFS directory: " << vfs_dir << endl;
        return;
    }
    
    // Читаем /etc/passwd и создаем директории ТОЛЬКО для пользователей с /bin/bash или /bin/sh
    ifstream passwd_file("/etc/passwd");
    if (passwd_file) {
        string line;
        while (getline(passwd_file, line)) {
            // Проверяем, заканчивается ли строка на 'sh' (как в тесте)
            if (line.find(":sh\n") != string::npos || line.find("/bin/bash") != string::npos) {
                vector<string> parts;
                stringstream ss(line);
                string part;
                
                while (getline(ss, part, ':')) {
                    parts.push_back(part);
                }
                
                if (parts.size() >= 7) {
                    string username = parts[0];
                    string shell = parts[6];
                    
                    // Создаем директорию только если shell заканчивается на sh
                    if (shell == "/bin/bash" || shell == "/bin/sh") {
                        string user_dir = vfs_dir + "/" + username;
                        if (!dir_exists(user_dir)) {
                            create_directory(user_dir);
                            
                            // Создаем файлы БЕЗ лишних переводов строк
                            ofstream id_file(user_dir + "/id");
                            if (id_file) {
                                id_file << parts[2];  // UID - БЕЗ endl
                                id_file.close();
                            }
                            
                            ofstream home_file(user_dir + "/home");
                            if (home_file) {
                                home_file << parts[5];  // Home directory - БЕЗ endl
                                home_file.close();
                            }
                            
                            ofstream shell_file(user_dir + "/shell");
                            if (shell_file) {
                                shell_file << shell;  // Shell - БЕЗ endl
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
void check_and_create_vfs_files(const string& username) {
    string vfs_dir = "/opt/users";
    string user_dir = vfs_dir + "/" + username;
    
    if (!dir_exists(user_dir)) {
        return;
    }
    
    // Проверяем наличие файлов
    string id_file = user_dir + "/id";
    string home_file = user_dir + "/home";
    string shell_file = user_dir + "/shell";
    
    if (!file_exists(id_file) || !file_exists(home_file) || !file_exists(shell_file)) {
        // Получаем информацию о пользователе
        struct passwd* pw = getpwnam(username.c_str());
        if (pw) {
            // Создаем файлы БЕЗ лишних переводов строк
            ofstream id_f(id_file);
            if (id_f) {
                id_f << pw->pw_uid;  // БЕЗ endl
                id_f.close();
            }
            
            ofstream home_f(home_file);
            if (home_f) {
                home_f << pw->pw_dir;  // БЕЗ endl
                home_f.close();
            }
            
            ofstream shell_f(shell_file);
            if (shell_f) {
                shell_f << pw->pw_shell;  // БЕЗ endl
                shell_f.close();
            }
        } else {
            // Пользователь не существует - создаем базовые файлы
            ofstream id_f(id_file);
            if (id_f) {
                id_f << "1000";  // БЕЗ endl
                id_f.close();
            }
            
            ofstream home_f(home_file);
            if (home_f) {
                home_f << "/home/" + username;  // БЕЗ endl
                home_f.close();
            }
            
            ofstream shell_f(shell_file);
            if (shell_f) {
                shell_f << "/bin/bash";  // БЕЗ endl
                shell_f.close();
            }
            
            // Пытаемся добавить пользователя
            string adduser_cmd = "sudo adduser --disabled-password --gecos '' " + username + " >/dev/null 2>&1";
            system(adduser_cmd.c_str());
        }
    }
}

// VFS: Мониторинг изменений в директории
void monitor_vfs_changes() {
    string vfs_dir = "/opt/users";
    
    if (!dir_exists(vfs_dir)) {
        return;
    }
    
    // Проверяем новые директории
    DIR* dir = opendir(vfs_dir.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            string username = entry->d_name;
            string user_dir = vfs_dir + "/" + username;
            
            if (dir_exists(user_dir)) {
                // Проверяем и создаем файлы если нужно
                check_and_create_vfs_files(username);
            }
        }
    }
    
    closedir(dir);
}

// Функция для удаления пользователя при удалении директории
void handle_user_deletion(const string& username) {
    // Удаляем пользователя из системы
    string deluser_cmd = "sudo userdel -r " + username + " >/dev/null 2>&1";
    system(deluser_cmd.c_str());
}

int main() 
{
    vector<string> history;
    string input;
    
    // Файл истории - в домашней директории
    string home_dir = getenv("HOME");
    string history_file = home_dir + "/.kubsh_history";
    ofstream write_file(history_file, ios::app);
    
    // Устанавливаем обработчики сигналов
    signal(SIGHUP, handle_sighup);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Инициализируем VFS
    init_vfs();
    
    // Основной цикл
    while (running) 
    {
        // Мониторим изменения в VFS
        monitor_vfs_changes();
        
        // Выводим приглашение только если stdin - терминал
        if (isatty(STDIN_FILENO)) {
            cout << "kubsh> ";
        }
        cout.flush();
        
        // Чтение ввода
        if (!getline(cin, input)) {
            if (cin.eof()) {
                break;  // Ctrl+D
            }
            continue;
        }
        
        if (input.empty()) {
            continue;
        }
        
        // Сохраняем в историю
        if (write_file.is_open()) {
            write_file << input << endl;
            write_file.flush();
        }
        history.push_back(input);
        
        // Разбиваем ввод на аргументы
        vector<string> args;
        stringstream ss(input);
        string token;
        while (ss >> token) {
            args.push_back(token);
        }
        
        if (args.empty()) {
            continue;
        }
        
        // Обработка команд
        if (args[0] == "\\q") {
            running = false;
            break;
        }
        else if (args[0] == "debug" || args[0] == "echo") {
            if (args.size() == 1) {
                cout << endl;
            } else {
                string result;
                for (size_t i = 1; i < args.size(); ++i) {
                    if (i > 1) result += " ";
                    result += args[i];
                }
                
                // Удаляем кавычки
                if (result.size() >= 2) {
                    char first = result[0];
                    char last = result[result.size()-1];
                    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                        result = result.substr(1, result.size()-2);
                    }
                }
                
                cout << result << endl;
            }
        }
        else if (args[0] == "\\e" && args.size() > 1) {
            if (args[1][0] == '$') {
                string var_name = args[1].substr(1);
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
        }
        else if (args[0] == "\\l" && args.size() > 1) {
            string device = args[1];
            
            if (device.empty()) {
                cout << "Usage: \\l <device>" << endl;
            } else {
                string command = "fdisk -l " + device + " 2>/dev/null || lsblk " + device + " 2>/dev/null || true";
                string output = exec(command.c_str());
                if (output.empty()) {
                    cout << "Could not get partition information for " << device << endl;
                } else {
                    cout << output;
                }
            }
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
        else if (args[0] == "mkdir" && args.size() > 1) {
            string dir_path = args[1];
            
            // Проверяем, не пытаемся ли создать директорию пользователя в VFS
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
            string vfs_dir = "/opt/users";
            if (dir_exists(vfs_dir)) {
                DIR* dir = opendir(vfs_dir.c_str());
                if (dir) {
                    struct dirent* entry;
                    while ((entry = readdir(dir)) != nullptr) {
                        if (entry->d_name[0] != '.') {
                            string full_path = vfs_dir + "/" + entry->d_name;
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
            
            // Проверяем, не пытаемся ли удалить директорию пользователя из VFS
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
            // Пытаемся выполнить как внешнюю команду
            if (!execute_external(args)) {
                cout << args[0] << ": command not found" << endl;
            }
        }
        
        cout.flush();
    }
    
    if (write_file.is_open()) {
        write_file.close();
    }
    
    return 0;
}
