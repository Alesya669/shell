#define FUSE_USE_VERSION 35

#include <unistd.h>        // fork в run_cmd
#include <cstdlib>         // NULL 
#include <cstring>         
#include <pwd.h>           
#include <sys/types.h>     
#include <cerrno>          
#include <ctime>           
#include <string>
#include "vfs.hpp"         //  fuse_start 
#include <sys/wait.h>      // waitpid
#include <fuse3/fuse.h>
#include <pthread.h>       // Потоки

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================

int run_cmd(const char* cmd, char* const argv[]) {
    pid_t pid = fork();

    if (pid == 0) {
        execvp(cmd, argv);  // Выполнили - отдали управление
        _exit(127);         // Иначе ошибка
    }

    int status = 0;
    waitpid(pid, &status, 0);

    // Проверка завершения процесса и статуса, если все хорошо то return 0 иначе ошибка -1
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;

    return -1;
}

// Для проверки на "правильность" шелла
// pwd - структура passwd, в которой есть указатели на конкретные данные из файла
bool valid_shell(struct passwd* pwd) {
    if (!pwd || !pwd->pw_shell) 
        return false;
    
    size_t len = strlen(pwd->pw_shell);
    // Если название шелла >=2 и последние 2 символа в названии == sh 
    return (len >= 2 && strcmp(pwd->pw_shell + len - 2, "sh") == 0);
}

// ============================================================================
// FUSE ОПЕРАЦИИ
// ============================================================================

// Проверка существования пути, получения прав доступа
int users_getattr(const char* path, struct stat* st, struct fuse_file_info* fi) {
    (void) fi;
    
    // Обнуление полей st
    memset(st, 0, sizeof(struct stat));
    
    // Ставим время изменения, доступа и модификации на текущее
    time_t now = time(NULL);
    st->st_atime = st->st_mtime = st->st_ctime = now;

    // Если корневая директория - владелец текущий пользователь
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;  // Права rwxr-xr-x
        st->st_uid = getuid();
        st->st_gid = getgid();
        return 0;
    }

    char username[256];
    char filename[256];

    // Файлы в директориях пользователей
    // Разбиваем path на /...(255)/...
    // Если удачно то кладем первую часть в username, вторую в filename 
    if (sscanf(path, "/%255[^/]/%255[^/]", username, filename) == 2) {
        // getpwnam ищем в pwd пользователя username
        struct passwd* pwd = getpwnam(username);

        // Если файл это id/home/shell, иначе файл не найден
        if (strcmp(filename, "id") != 0 &&
            strcmp(filename, "home") != 0 &&
            strcmp(filename, "shell") != 0) {
            return -ENOENT;
        }

        // Если нашли в pwd(то есть не NULL)
        if (pwd != NULL) {
            st->st_mode = S_IFREG | 0644;  // Обычный файл с правами rw-r--r--
            st->st_uid = pwd->pw_uid;      // Владелец - пользователь
            st->st_gid = pwd->pw_gid;
            st->st_size = 256;             // Размер файла 256 байт
            return 0;
        }
        return -ENOENT;
    }

    // Директории пользователей
    // Если разбили path только на /...
    if (sscanf(path, "/%255[^/]", username) == 1) {
        struct passwd* pwd = getpwnam(username);
        if (pwd != NULL) {
            st->st_mode = S_IFDIR | 0755;
            st->st_uid = pwd->pw_uid;  // Владелец - пользователь
            st->st_gid = pwd->pw_gid;
            return 0;
        }
        return -ENOENT;
    }

    return -ENOENT;
}

int users_readdir(
    const char* path,
    void* buf, 
    fuse_fill_dir_t filler, 
    off_t offset, 
    struct fuse_file_info* fi, 
    enum fuse_readdir_flags flags
) {
    (void) offset;
    (void) fi;
    (void) flags;

    // filler - функция, которая добавляет одну запись в виртуальную директорию
    filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);

    // Если в корне, то смотрим все user'ов в passwd, и пока находим новых проверяем их на шелл
    // и заполняем buf в который ложим записи 
    if (std::strcmp(path, "/") == 0) {
        struct passwd* pwd;
        setpwent();

        while ((pwd = getpwent()) != NULL) {
            if (valid_shell(pwd)) {
                // buf - буфер куда ложим записи, pwd->pw_name - имя файла или директории
                filler(buf, pwd->pw_name, NULL, 0, FUSE_FILL_DIR_PLUS);
            }
        }

        endpwent();
        return 0;
    }

    char username[256] = {0};
    if (sscanf(path, "/%255[^/]", username) == 1) {
        struct passwd* pwd = getpwnam(username);
        if (pwd != NULL) {
            // Складываем все файлы в каждом user в буфер
            filler(buf, "id", NULL, 0, FUSE_FILL_DIR_PLUS);
            filler(buf, "home", NULL, 0, FUSE_FILL_DIR_PLUS);
            filler(buf, "shell", NULL, 0, FUSE_FILL_DIR_PLUS);
            return 0;
        }
    }
    
    // После отработки функции все что было в buf отправится в файловую систему
    return -ENOENT;
}

int users_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) fi;

    char username[256];
    char filename[256];

    // Разбиваем path на 2 части: имя и файл (id/dir/shell)
    std::sscanf(path, "/%255[^/]/%255[^/]", username, filename);

    // Ищем в pwd информацию о username
    struct passwd* pwd = getpwnam(username);
    if(!pwd) return -ENOENT;
    
    char content[256];
    content[0] = '\0';

    if (std::strcmp(filename, "id") == 0) {
        // content - куда записываем, 256 байт максимум, %d - целое число, берем из pw_uid
        std::snprintf(content, sizeof(content), "%d", pwd->pw_uid);
    }
    else if (std::strcmp(filename, "home") == 0) {
        // %s - строка
        std::snprintf(content, sizeof(content), "%s", pwd->pw_dir);
    }
    else {
        std::snprintf(content, sizeof(content), "%s", pwd->pw_shell);
    }

    size_t len = std::strlen(content);
    if (len > 0 && content[len-1] == '\n') {
        content[len-1] = '\0';
        len--;
    }

    // Проверка чтобы не читали за пределом файла
    if ((size_t)offset >= len) {
        return 0;
    }

    // Указываем сколько байт можно прочитать
    if (offset + size > len) {
        size = len - offset;
    }

    // Копируем данные в буфер 
    std::memcpy(buf, content + offset, size);
    
    // Возврат сколько байт прочитали
    return size;
}

int users_mkdir(const char* path, mode_t mode) {
    (void) mode;

    char username[256];

    // Если извлекли только имя пользователя из path
    if (std::sscanf(path, "/%255[^/]", username) == 1) {
        // Открываем pwd и ищем username
        struct passwd* pwd = getpwnam(username);
        
        // Возврат если такой пользователь уже существует 
        if (pwd != NULL) {
            return -EEXIST;
        }

        // Строки которые передадим для выполнения через run_cmd (fork)  
        char* const argv[] = {
            (char*)"adduser", 
            (char*)"--disabled-password",
            (char*)"--gecos", 
            (char*)"", 
            (char*)username, 
            NULL
        };

        if (run_cmd("adduser", argv) != 0) 
            return -EIO;
    }

    return 0;
}

int users_rmdir(const char* path) {
    char username[256];
    
    // Если извлекли только имя пользователя из path
    if (std::sscanf(path, "/%255[^/]", username) == 1) {
        // Проверка есть ли вложенные файлы в path
        // Если не находим "/" в path не считая первый (/.../ <-- типо такого)
        if (std::strchr(path + 1, '/') == NULL) {
            struct passwd* pwd = getpwnam(username);
            if (pwd != NULL) {
                char* const argv[] = {
                    (char*)"userdel", 
                    (char*)"--remove", 
                    (char*)username, 
                    NULL
                };

                if (run_cmd("userdel", argv) != 0) 
                    return -EIO;

                return -EIO;
            }
            return -ENOENT;
        }
        return -EPERM;
    }
    return -EPERM;
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ FUSE ОПЕРАЦИЙ
// ============================================================================

// Структура в которой описаны функции которые переопределим для vfs
// Инициализирую все нулями, потом с помощью функции переопределю нужные 5
struct fuse_operations users_operations = {};

void init_users_operations() {
    users_operations.getattr = users_getattr;
    users_operations.readdir = users_readdir;
    users_operations.mkdir   = users_mkdir;
    users_operations.rmdir   = users_rmdir;
    users_operations.read    = users_read;
}

// ============================================================================
// ПОТОК ДЛЯ FUSE
// ============================================================================

void* fuse_thread_function(void* arg) {
    (void) arg;

    // Вызов функции для инициализации
    init_users_operations();

    // Отключение лишних логов
    int devnull = open("/dev/null", O_WRONLY);
    int olderr = dup(STDERR_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);

    // Аргументы для fuse_main
    char* fuse_argv[] = {
        (char*) "kubsh",                    // Имя программы
        (char*) "-f",
        (char*) "-odefault_permissions",    // Стандартные права доступа
        (char*) "-oauto_unmount",           // Автоматическое размонтирование
        (char*) "/opt/users"                // Куда монтируем
    };

    // Количество аргументов
    int fuse_argc = sizeof(fuse_argv) / sizeof(fuse_argv[0]);

    // Первые два аргумента - передаем запуск будто из командой строки
    // users_operations - структура с функциями
    // Последний аргумент нам не нужен
    fuse_main(fuse_argc, (char**)fuse_argv, &users_operations, nullptr);

    // Возврат логов
    dup2(olderr, STDERR_FILENO);
    close(olderr);

    return nullptr;
}

// ============================================================================
// ОСНОВНАЯ ФУНКЦИЯ ЗАПУСКА
// ============================================================================

void fuse_start() {
    // Создаем поток fuse_thread
    pthread_t fuse_thread;

    // Запускаем в этом потоке функцию в которой fuse_main
    // Это нужно чтобы vfs не блокировала работу шелла
    pthread_create(&fuse_thread, nullptr, fuse_thread_function, nullptr);
}
