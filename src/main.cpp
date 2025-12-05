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
#include <cstring>

using namespace std;
// Глобальная переменная для обработки сигналов
volatile sig_atomic_t running = true;

// Обработчик сигнала SIGHUP для перезагрузки конфигурации
void handle_sighup(int signum) {
    (void)signum; // Подавляем предупреждение о неиспользуемом параметре
    cout << "Configuration reloaded" << endl;
}

// Основной обработчик сигналов
void signal_handler(int signum) {
    if (signum == SIGHUP) {
        // Для SIGHUP не останавливаем программу
        return;
    } else {
        running = false; // Останавливаем программу для других сигналов
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

// Рекурсивное создание директории
bool create_directory(const string& path) {
    // Проверка, существует ли директория
    if (dir_exists(path)) return true;
    
    // Создание родительских директорий
    size_t pos = path.find_last_of('/');
    if (pos != string::npos) {
        string parent = path.substr(0, pos);
        if (!parent.empty() && !create_directory(parent)) {
            return false;
        }
    }
    
    // Создание директории
    return mkdir(path.c_str(), 0755) == 0;
}

// Поиск команды в PATH
string find_command(const string& cmd) {
    // Проверка, содержит ли команда путь
    if (cmd.find('/') != string::npos) {
        if (file_exists(cmd)) {
            return cmd;
        }
        return "";
    }
    
    // Поиск в переменной PATH
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

// Выполнение команды
bool execute_command(const string& cmd) {
    vector<string> args;
    stringstream ss(cmd);
    string token;
    
    // Разбиваем команду на аргументы
    while (getline(ss, token, ' ')) {
        if (!token.empty()) {
            args.push_back(token);
        }
    }
    
    if (args.empty()) return false;
    
    // Проверка встроенных команд
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
    
    // Поиск и выполнение команды
    string command_path = find_command(args[0]);
    if (command_path.empty()) {
        // Проверка на команду ls для VFS пользователей
        if (args[0] == "ls" && args.size() == 2 && args[1] == "/opt/users") {
            // Вывод списка пользователей VFS
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
        
        // Проверка на чтение /etc/passwd
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
    
    // Подготовка аргументов для execv
    vector<char*> exec_args;
    for (auto& arg : args) {
        exec_args.push_back(const_cast<char*>(arg.c_str()));
    }
    exec_args.push_back(nullptr);
    
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        execv(command_path.c_str(), exec_args.data());
        perror("execv failed");
        exit(1);
    } else if (pid > 0) {
        // Родительский процесс
        waitpid(pid, nullptr, 0);
        return true;
    } else {
        perror("fork failed");
        return false;
    }
}

// Создание пользователя в VFS
void create_vfs_user(const string& username) {
    string user_dir = "/opt/users/" + username;
    create_directory(user_dir);
}

int main() 
{
    vector<string> history;
    string input;
    string history_file = "kubsh_history.txt";
    ofstream write_file(history_file, ios::app);
    
    // НАСТРОЙКА ОБРАБОТЧИКОВ СИГНАЛОВ
    // Используем простой signal() для SIGHUP
    signal(SIGHUP, handle_sighup);  // Для SIGHUP используем handle_sighup
    
    // Для других сигналов используем отдельный обработчик
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Создание директории /opt/users если её нет
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
        
        // Проверка команды выхода
        if (input == "\\q") 
        {
            running = false;
            break;
        }
        // Обработка команды debug
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
        // Обработка вывода переменных окружения
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
        // Обработка команд с абсолютным путем
        else if (input[0] == '/')  
        {
            pid_t pid = fork();
            if (pid == 0) {
                // Дочерний процесс
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
        // Обработка всех остальных команд
        else 
        {
            if (!execute_command(input)) {
                cout << input << ": command not found" << endl;
            }
            history.push_back(input);
        }
        
        // Проверка состояния stdin (для обработки сигналов)
        if (!cin.good()) {
            running = false;
        }
    }
    
    write_file.close();
    return 0;
}
