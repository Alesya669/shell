#include "vfs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mount.h>

using namespace std;

VFS::VFS(const string& root_path) : vfs_root(root_path) {}

bool VFS::dir_exists(const string& path) const {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) return false;
    return S_ISDIR(buffer.st_mode);
}

bool VFS::file_exists(const string& path) const {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool VFS::create_directory(const string& path) {
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

void VFS::create_user_info(const string& username) {
    string user_dir = vfs_root + "/" + username;
    
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
        
        // Пытаемся добавить пользователя в систему
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

void VFS::delete_user(const string& username) {
    // Удаляем пользователя из системы
    string deluser_cmd = "sudo userdel -r " + username + " >/dev/null 2>&1";
    system(deluser_cmd.c_str());
}

bool VFS::initialize() {
    // Создаем корневую директорию VFS
    if (!create_directory(vfs_root)) {
        cerr << "Failed to create VFS root directory: " << vfs_root << endl;
        return false;
    }
    
    // Синхронизируем с /etc/passwd
    sync_from_passwd();
    
    return true;
}

bool VFS::mount() {
    // В реальной системе здесь была бы команда mount
    // Для упрощения просто создаем директорию и файлы
    cout << "Mounting VFS at: " << vfs_root << endl;
    return initialize();
}

bool VFS::unmount() {
    cout << "Unmounting VFS from: " << vfs_root << endl;
    return true;
}

void VFS::sync_from_passwd() {
    ifstream passwd_file("/etc/passwd");
    if (!passwd_file) {
        cerr << "Failed to open /etc/passwd" << endl;
        return;
    }
    
    string line;
    while (getline(passwd_file, line)) {
        vector<string> parts;
        stringstream ss(line);
        string part;
        
        while (getline(ss, part, ':')) {
            parts.push_back(part);
        }
        
        if (parts.size() >= 7) {
            string username = parts[0];
            string shell = parts[6];
            
            // Создаем директорию только если shell заканчивается на sh (bash/sh)
            if (shell == "/bin/bash" || shell == "/bin/sh" || shell == "/usr/bin/bash" || shell == "/usr/bin/sh") {
                string user_dir = vfs_root + "/" + username;
                if (!dir_exists(user_dir)) {
                    create_directory(user_dir);
                    
                    // Создаем файлы с информацией
                    ofstream id_file(user_dir + "/id");
                    if (id_file) {
                        id_file << parts[2];  // UID
                        id_file.close();
                    }
                    
                    ofstream home_file(user_dir + "/home");
                    if (home_file) {
                        home_file << parts[5];  // Home directory
                        home_file.close();
                    }
                    
                    ofstream shell_file(user_dir + "/shell");
                    if (shell_file) {
                        shell_file << shell;  // Shell
                        shell_file.close();
                    }
                }
            }
        }
    }
    passwd_file.close();
}

void VFS::monitor_changes() {
    if (!dir_exists(vfs_root)) {
        return;
    }
    
    // Проверяем новые директории
    DIR* dir = opendir(vfs_root.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            string username = entry->d_name;
            string user_dir = vfs_root + "/" + username;
            
            if (dir_exists(user_dir)) {
                // Проверяем наличие обязательных файлов
                string id_file = user_dir + "/id";
                string home_file = user_dir + "/home";
                string shell_file = user_dir + "/shell";
                
                if (!file_exists(id_file) || !file_exists(home_file) || !file_exists(shell_file)) {
                    // Создаем недостающие файлы
                    create_user_info(username);
                }
            }
        }
    }
    
    closedir(dir);
}

bool VFS::create_user_dir(const string& username) {
    if (username.empty() || username.find('/') != string::npos) {
        return false;
    }
    
    create_user_info(username);
    return true;
}

bool VFS::remove_user_dir(const string& username) {
    string user_dir = vfs_root + "/" + username;
    
    if (!dir_exists(user_dir)) {
        return false;
    }
    
    // Удаляем пользователя из системы
    delete_user(username);
    
    // Удаляем директорию
    string cmd = "rm -rf \"" + user_dir + "\"";
    return system(cmd.c_str()) == 0;
}

vector<string> VFS::list_users() const {
    vector<string> users;
    
    if (!dir_exists(vfs_root)) {
        return users;
    }
    
    DIR* dir = opendir(vfs_root.c_str());
    if (!dir) return users;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            string full_path = vfs_root + "/" + entry->d_name;
            if (dir_exists(full_path)) {
                users.push_back(entry->d_name);
            }
        }
    }
    
    closedir(dir);
    return users;
}

bool VFS::is_user_login_allowed(const string& username) const {
    string user_dir = vfs_root + "/" + username;
    if (!dir_exists(user_dir)) {
        return false;
    }
    
    // Проверяем файл shell
    string shell_file = user_dir + "/shell";
    ifstream file(shell_file);
    if (!file) {
        return false;
    }
    
    string shell;
    getline(file, shell);
    
    // Разрешаем логин только для bash/sh shell
    return (shell == "/bin/bash" || shell == "/bin/sh" || 
            shell == "/usr/bin/bash" || shell == "/usr/bin/sh");
}
