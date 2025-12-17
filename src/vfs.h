#ifndef VFS_H
#define VFS_H

#include <string>
#include <vector>
#include <sys/stat.h>

class VFS {
private:
    std::string vfs_root;
    
    bool create_directory(const std::string& path);
    bool dir_exists(const std::string& path) const;
    bool file_exists(const std::string& path) const;
    void create_user_info(const std::string& username);
    void delete_user(const std::string& username);
    
public:
    VFS(const std::string& root_path = "/opt/users");
    
    bool initialize();
    bool mount();
    bool unmount();
    void sync_from_passwd();
    void monitor_changes();
    bool create_user_dir(const std::string& username);
    bool remove_user_dir(const std::string& username);
    std::vector<std::string> list_users() const;
    bool is_user_login_allowed(const std::string& username) const;
    
    std::string get_root() const { return vfs_root; }
};

#endif // VFS_H
