#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>

using namespace std;

// Флажок для работы шелла
bool shell_works = true;

// Обработчик сигнала SIGHUP
void sighup_handler(int) {
    cout << "Configuration reloaded" << endl;
}

// Обработчик других сигналов
void signal_handler(int) {
    shell_works = false;
}

// Есть ли файл?
bool file_exists(string path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

// Есть ли папка?
bool folder_exists(string path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) return false;
    return S_ISDIR(info.st_mode);
}

// Создать папку
bool create_folder(string path) {
    if (folder_exists(path)) return true;
    
    size_t pos = path.find_last_of('/');
    if (pos != string::npos) {
        string parent = path.substr(0, pos);
        if (!parent.empty() && !create_folder(parent)) {
            return false;
        }
    }
    
    return mkdir(path.c_str(), 0755) == 0;
}

// Найти команду в PATH
string find_in_path(string cmd) {
    // Если команда уже с путём
    if (cmd.find('/') != string::npos) {
        if (file_exists(cmd)) return cmd;
        return "";
    }
    
    // Ищем в PATH
    const char* path_env = getenv("PATH");
    if (!path_env) return "";
    
    stringstream ss(path_env);
    string path_part;
    
    while (getline(ss, path_part, ':')) {
        string full = path_part + "/" + cmd;
        if (file_exists(full)) {
            return full;
        }
    }
    
    return "";
}

// ========== VFS ДЛЯ ПОЛЬЗОВАТЕЛЕЙ ==========

// Где будет папка users
string get_users_folder() {
    const char* home = getenv("HOME");
    if (!home) return "./users";
    return string(home) + "/users";
}

// Создать папку для пользователя в VFS
void make_user_folder(string username) {
    string users_dir = get_users_folder();
    string user_dir = users_dir + "/" + username;
    
    // Создаём папку
    create_folder(user_dir);
    
    // Файл с ID
    ofstream id_file(user_dir + "/id");
    id_file << "1000" << endl; // Просто пример ID
    id_file.close();
    
    // Файл с домашней папкой
    ofstream home_file(user_dir + "/home");
    home_file << "/home/" + username << endl;
    home_file.close();
    
    // Файл с шеллом
    ofstream shell_file(user_dir + "/shell");
    shell_file << "/bin/bash" << endl;
    shell_file.close();
    
    // Создаём в системе (без sudo для простоты)
    string cmd = "useradd -m " + username + " 2>/dev/null || true";
    system(cmd.c_str());
}

// Удалить папку пользователя
void delete_user_folder(string username) {
    string users_dir = get_users_folder();
    string user_dir = users_dir + "/" + username;
    
    // Удаляем из системы
    string cmd = "userdel -r " + username + " 2>/dev/null || true";
    system(cmd.c_str());
    
    // Удаляем папку
    cmd = "rm -rf \"" + user_dir + "\"";
    system(cmd.c_str());
}

// Инициализация VFS
void setup_vfs() {
    string users_dir = get_users_folder();
    
    // Создаём главную папку
    if (!folder_exists(users_dir)) {
        cout << "Создаём папку users: " << users_dir << endl;
        create_folder(users_dir);
    }
    
    // Делаем ссылку /opt/users
    if (!folder_exists("/opt/users")) {
        string cmd = "ln -sf \"" + users_dir + "\" /opt/users";
        system(cmd.c_str());
    }
    
    // Пример: создаём тестового пользователя
    make_user_folder("testuser");
}

// Выполнить команду
bool run_command(string cmd) {
    vector<string> parts;
    stringstream ss(cmd);
    string part;
    
    // Делим команду на части
    while (getline(ss, part, ' ')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    
    if (parts.empty()) return false;
    
    // Команда echo
    if (parts[0] == "echo") {
        if (parts.size() > 1) {
            string text;
            for (size_t i = 1; i < parts.size(); i++) {
                if (i > 1) text += " ";
                text += parts[i];
            }
            cout << text << endl;
        } else {
            cout << endl;
        }
        return true;
    }
    
    // Команда cat
    if (parts[0] == "cat") {
        if (parts.size() < 2) {
            cout << "Нужно указать файл" << endl;
            return true;
        }
        
        ifstream file(parts[1]);
        if (file) {
            string line;
            while (getline(file, line)) {
                cout << line << endl;
            }
            return true;
        } else {
            cout << "Файл не найден: " << parts[1] << endl;
            return true;
        }
    }
    
    // Добавить пользователя
    if (parts[0] == "adduser") {
        if (parts.size() < 2) {
            cout << "Укажи имя пользователя" << endl;
            return true;
        }
        make_user_folder(parts[1]);
        cout << "Пользователь " << parts[1] << " создан" << endl;
        return true;
    }
    
    // Удалить пользователя
    if (parts[0] == "userdel") {
        if (parts.size() < 2) {
            cout << "Укажи имя пользователя" << endl;
            return true;
        }
        delete_user_folder(parts[1]);
        cout << "Пользователь " << parts[1] << " удалён" << endl;
        return true;
    }
    
    // Ищем команду в системе
    string cmd_path = find_in_path(parts[0]);
    if (cmd_path.empty()) {
        return false;
    }
    
    // Подготавливаем аргументы
    vector<char*> args;
    for (auto& p : parts) {
        args.push_back(const_cast<char*>(p.c_str()));
    }
    args.push_back(nullptr);
    
    // Запускаем
    pid_t pid = fork();
    if (pid == 0) {
        // Это дочерний процесс
        execv(cmd_path.c_str(), args.data());
        cout << "Ошибка запуска" << endl;
        exit(1);
    } else if (pid > 0) {
        // Это родительский процесс
        waitpid(pid, nullptr, 0);
        return true;
    } else {
        cout << "Ошибка fork" << endl;
        return false;
    }
}

int main() 
{
    vector<string> history;
    string input;
    string history_file = "kubsh_history.txt";
    
    // Настраиваем сигналы
    signal(SIGHUP, sighup_handler);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Настраиваем VFS
    setup_vfs();
    
   
    
    while (shell_works && getline(cin, input)) 
    {
        if (input.empty()) {
            cout << "kubsh> ";
            continue;
        }
        
        // Сохраняем в историю
        ofstream hist_file(history_file, ios::app);
        hist_file << input << endl;
        hist_file.close();
        
        // Выход
        if (input == "\\q") 
        {
            shell_works = false;
            break;
        }
        
        // Показать переменную окружения
        else if (input.substr(0,4) == "\\e $") 
        {
            string var_name = input.substr(4);
            const char* value = getenv(var_name.c_str());
            
            if (value != nullptr) {
                cout << value << endl;
            } else {
                cout << "Нет такой переменной" << endl;
            }
        }
        
        // Информация о диске
        else if (input.find("\\l ") == 0) 
        {
            string disk = input.substr(3);
            
            if (!file_exists(disk)) {
                cout << "Диск не найден" << endl;
            } else {
                string cmd = "fdisk -l " + disk + " 2>/dev/null";
                system(cmd.c_str());
            }
        }
        
        // Команда с путём
        else if (input[0] == '/')  
        {
            vector<string> parts;
            stringstream ss(input);
            string part;
            
            while (getline(ss, part, ' ')) {
                parts.push_back(part);
            }
            
            vector<char*> args;
            for (auto& p : parts) {
                args.push_back(const_cast<char*>(p.c_str()));
            }
            args.push_back(nullptr);
            
            pid_t pid = fork();
            if (pid == 0) {
                execv(args[0], args.data());
                cout << "Не запустилось" << endl;
                exit(1);
            } else if (pid > 0) {
                waitpid(pid, nullptr, 0);
            }
        }
        
        // Обычная команда
        else 
        {
            if (!run_command(input)) {
                cout << "Команда не найдена: " << input << endl;
            }
        }
        
        cout << "kubsh> ";
    }
    
    return 0;
}
