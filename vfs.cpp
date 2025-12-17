#define FUSE_USE_VERSION 35

#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <sys/types.h>
#include <cerrno>
#include <ctime>
#include <string>
#include <sys/wait.h>
#include <fuse3/fuse.h>
#include <pthread.h>
#include "vfs.h"

// ==================== Вспомогательные функции ====================

static int run_cmd(const char* cmd, char* const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp(cmd, argv);
        _exit(127);
    }
    
    int status = 0;
    waitpid(pid, &status, 0);
    
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static bool valid_shell(struct passwd* pwd) {
    if (!pwd || !pwd->pw_shell) 
        return false;
    
    size_t len = strlen(pwd->pw_shell);
    return (len >= 2 && strcmp(pwd->pw_shell + len - 2, "sh") == 0);
}

// ==================== FUSE операции ====================

static int users_getattr(const char* path, struct stat* st, struct fuse_file_info* fi) {
    (void)fi;
    memset(st, 0, sizeof(struct stat));
    
    time_t now = time(NULL);
    st->st_atime = st->st_mtime = st->st_ctime = now;

    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_uid = getuid();
        st->st_gid = getgid();
        return 0;
    }

    char username[256];
    char filename[256];

    if (sscanf(path, "/%255[^/]/%255[^/]", username, filename) == 2) {
        struct passwd* pwd = getpwnam(username);
        if (!pwd) return -ENOENT;

        if (strcmp(filename, "id") != 0 &&
            strcmp(filename, "home") != 0 &&
            strcmp(filename, "shell") != 0) {
            return -ENOENT;
        }

        st->st_mode = S_IFREG | 0644;
        st->st_uid = pwd->pw_uid;
        st->st_gid = pwd->pw_gid;
        st->st_size = 256;
        return 0;
    }

    if (sscanf(path, "/%255[^/]", username) == 1) {
        struct passwd* pwd = getpwnam(username);
        if (pwd) {
            st->st_mode = S_IFDIR | 0755;
            st->st_uid = pwd->pw_uid;
            st->st_gid = pwd->pw_gid;
            return 0;
        }
        return -ENOENT;
    }

    return -ENOENT;
}

static int users_readdir(const char* path, void* buf, fuse_fill_dir_t filler, 
                         off_t offset, struct fuse_file_info* fi, 
                         enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);

    if (strcmp(path, "/") == 0) {
        struct passwd* pwd;
        setpwent();
        
        while ((pwd = getpwent()) != NULL) {
            if (valid_shell(pwd)) {
                filler(buf, pwd->pw_name, NULL, 0, FUSE_FILL_DIR_PLUS);
            }
        }
        
        endpwent();
        return 0;
    }

    char username[256] = {0};
    if (sscanf(path, "/%255[^/]", username) == 1) {
        struct passwd* pwd = getpwnam(username);
        if (pwd) {
            filler(buf, "id", NULL, 0, FUSE_FILL_DIR_PLUS);
            filler(buf, "home", NULL, 0, FUSE_FILL_DIR_PLUS);
            filler(buf, "shell", NULL, 0, FUSE_FILL_DIR_PLUS);
            return 0;
        }
    }
    
    return -ENOENT;
}

static int users_read(const char* path, char* buf, size_t size, off_t offset, 
                      struct fuse_file_info* fi) {
    (void)fi;
    
    char username[256];
    char filename[256];
    
    if (sscanf(path, "/%255[^/]/%255[^/]", username, filename) != 2) {
        return -ENOENT;
    }
    
    struct passwd* pwd = getpwnam(username);
    if (!pwd) return -ENOENT;
    
    char content[256];
    content[0] = '\0';
    
    if (strcmp(filename, "id") == 0) {
        snprintf(content, sizeof(content), "%d", pwd->pw_uid);
    } else if (strcmp(filename, "home") == 0) {
        snprintf(content, sizeof(content), "%s", pwd->pw_dir);
    } else {
        snprintf(content, sizeof(content), "%s", pwd->pw_shell);
    }
    
    size_t len = strlen(content);
    if (offset >= (off_t)len) return 0;
    
    if (offset + size > len) size = len - offset;
    memcpy(buf, content + offset, size);
    return size;
}

static int users_mkdir(const char* path, mode_t mode) {
    (void)mode;
    
    char username[256];
    if (sscanf(path, "/%255[^/]", username) != 1) {
        return -EINVAL;
    }
    
    if (getpwnam(username) != NULL) {
        return -EEXIST;
    }
    
    char* const argv[] = {
        (char*)"adduser", 
        (char*)"--disabled-password",
        (char*)"--gecos", 
        (char*)"", 
        (char*)username, 
        NULL
    };
    
    return (run_cmd("adduser", argv) == 0) ? 0 : -EIO;
}

static int users_rmdir(const char* path) {
    char username[256];
    if (sscanf(path, "/%255[^/]", username) != 1) {
        return -EINVAL;
    }
    
    if (strchr(path + 1, '/') != NULL) {
        return -EPERM;
    }
    
    struct passwd* pwd = getpwnam(username);
    if (!pwd) {
        return -ENOENT;
    }
    
    char* const argv[] = {
        (char*)"userdel", 
        (char*)"--remove", 
        (char*)username, 
        NULL
    };
    
    return (run_cmd("userdel", argv) == 0) ? 0 : -EIO;
}

// ==================== Инициализация операций FUSE ====================

static struct fuse_operations users_operations = {
    .getattr = users_getattr,
    .readdir = users_readdir,
    .read = users_read,
    .mkdir = users_mkdir,
    .rmdir = users_rmdir,
};

// ==================== Поток для FUSE ====================

static void* fuse_thread_function(void* arg) {
    (void)arg;
    
    // Перенаправляем stderr в /dev/null
    int devnull = open("/dev/null", O_WRONLY);
    int olderr = dup(STDERR_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
    
    // Аргументы для FUSE
    char* fuse_argv[] = {
        (char*)"kubsh",
        (char*)"-f",
        (char*)"-odefault_permissions",
        (char*)"/opt/users"
    };
    
    int fuse_argc = sizeof(fuse_argv) / sizeof(fuse_argv[0]);
    fuse_main(fuse_argc, fuse_argv, &users_operations, NULL);
    
    // Восстанавливаем stderr
    dup2(olderr, STDERR_FILENO);
    close(olderr);
    
    return NULL;
}

// ==================== Основная функция запуска FUSE ====================

void fuse_start() {
    pthread_t fuse_thread;
    pthread_create(&fuse_thread, NULL, fuse_thread_function, NULL);
    pthread_detach(fuse_thread);
}
