#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>//unitbuf
#include <sys/wait.h>//Для waitpid
#include <vector>
#include<sstream>//Для iss
#include<signal.h>//Работа с сигналами
#include <cstdint>//Для uint32_t 

#include "vfs.hpp"//Подключение VFS, в частности функция fuse_start


//Функция для обработки сигнала
void sighup_handler(int signal_nubmer)
{
    //Если получаем номер SIGHUP(обычно 1)
    if(signal_nubmer==SIGHUP)
    {
        std::cout<<"Configuration reloaded\n";
        std::cout<<"$ ";
    }
}


//  \l /dev/sda
void check_disk_partitions(const std::string& device_path)
{
    //Создание потока для чтения файла, device_path - путь к устройству (в нашем случае /dev/sda)
    std::ifstream device(device_path, std::ios::binary);

    //Если ошибка открытия файла
    if (!device) 
    {
        std::cout << "Error: Cannot open device " << device_path << "\n";
        return;
    }
    

    //Буфер на 512 байт
    char sector[512];
    //Читаем из нашего файла с диском первые 512 байт в буфер
    device.read(sector, 512);
    

    //Если прочитали не 512 байт(например меньше) - ошибка чтения
    if (device.gcount() != 512) 
    {
        std::cout << "Error: Cannot read disk\n";
        return;
    }
    
    // Проверяем сигнатуру - 510 и 511 байты должны быть 0x55 и 0xAA соответственно, иначе это не MBR/GPT
    if ((unsigned char)sector[510] != 0x55 || (unsigned char)sector[511] != 0xAA) 
    {
        std::cout << "Error: Invalid disk signature\n";
        return;
    }
    
    // Определяем тип диска
    bool is_gpt = false;
    //Идем начиная с 446 байта(начало таблицы разделов), всего 4 записи и каждая по 16 байт
    //Нас интересует тип записи(4 байт в записи)
    for (int i = 0; i < 4; i++) 
    {
        //Если находим байт 0xEE это значит что он GPT Protective => это GPT
        if ((unsigned char)sector[446 + i * 16 + 4] == 0xEE) 
        {
            is_gpt = true;
            break;
        }
    }
    
    if (!is_gpt) 
    {
        // MBR - простой вывод
        for (int i = 0; i < 4; i++) 
        {
            //Начало каждого из 4 разделов считаем для каждого прохода цикла
            int offset = 446 + i * 16;

            //Опять смотрим тип как в проверке на GPT
            unsigned char type = sector[offset + 4];
            
            //Раздел не существует если тип равен нулю
            if (type != 0) {
                //uint32_t - это беззнаковое 32-битное целое число
                //Читаем 12-15 байт раздела, отвечающий за количество секторов
                //Берем их(4 байта) как 32 битное число с помощью    *(uint32_t*)&sector
                uint32_t num_sectors = *(uint32_t*)&sector[offset + 12];
                //1 сектор - 512 байт, в 1 MB 1024*1024 байт => 2048 секторов
                uint32_t size_mb = num_sectors / 2048;
                //Если первый байт равен 0x80 то bootable
                bool bootable = ((unsigned char)sector[offset] == 0x80);
                
                std::cout << "Partition " << (i + 1) << ": Size=" << size_mb << "MB, Bootable: ";
                if(bootable)
                    std::cout<<"Yes\n";
                else std::cout<<"No\n";
            }
        }
    } 

    else 
    {
        //  GPT - просто количество разделов
        //Читаем из диска вторые 512 байт, в них хранится информация о GPT диске
        device.read(sector, 512);
        //Если прочли 512 (проверка как и раньше) и при этом байты 0-7 == "EFI PART"
        if (device.gcount() == 512 && sector[0] == 'E' && sector[1] == 'F' && sector[2] == 'I' && sector[3] == ' ' && sector[4] == 'P' && sector[5] == 'A' &&
            sector[6] == 'R' && sector[7] == 'T') 
        {
            //Также переходим к чтению 4 байтов начиная с 80, тут количество записей в таблице разделов
            uint32_t num_partitions = *(uint32_t*)&sector[80];
            std::cout << "GPT partitions: " << num_partitions << "\n";
        } 

        else 
        {
            std::cout << "GPT partitions: unknown\n";
        }
    }
}

int main() 
{

    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    


    fuse_start();

    std::cerr << "$ ";


    //Сохранение пути в переменную для истории
    const char* home=std::getenv("HOME");
    std::string historyPath=std::string(home)+"/.kubsh_history";



    std::string input;

    //Если приходит сигнал с номером SIGHUP то вызываем sighup_handler
    signal(SIGHUP,sighup_handler);

    while (std::getline(std::cin, input))
    {
        //Запись в историю
        if(!input.empty())
        {
	        std::ofstream history(historyPath,std::ios::app);//Открываем в режиме добавления
	        history << input<<"\n";
 	    }


        //history
        if(input=="history")
        {
            //Читаем из файла который в historyPath пока не закончатся строки
            std::ifstream historyOutput(historyPath);
                std::string line;
                while(std::getline(historyOutput,line))
                {
                    std::cout<<line<<"\n";
                }
        }


        //  \q
        else if (input == "\\q")
        {
            break;
        }   


        //  \l /dev/sda     (запускать через sudo)
        else if (input.substr(0, 3) == "\\l ") 
        {
            std::string device_path = input.substr(3);
            // Убираем возможные пробелы
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

        //  echo
        //Если начинаем debug '
        // заканчиваем ', считываем с открытия апострофа до закрытия
        else if (input.substr(0, 7) == "debug '" && input[input.length() - 1] == '\'')
        {

            std::cout << input.substr(7, input.length() - 8) << std::endl;  
            continue;  

        }



        //   \e $
        else if (input.substr(0,4) == "\\e $")
        {
            std::string varName = input.substr(4);
            const char* value = std::getenv(varName.c_str());//Преобразуем C-строку в C++ строку

            if(value != nullptr)
            {
                std::string valueStr = value;
                
                bool has_colon = false;//Флаг для проверки наличия двоеточий
                for (char c : valueStr)//Проходим по символам из строки
                {
                    if (c == ':') 
                    {
                        has_colon = true;
                        break;
                    }
                }
                
                if (has_colon) 
                {
                    std::string current_part = "";//Временная строка для накопления текущей части пути
                    for (char c : valueStr)//Разбиваем строку по двоеточиям

                    {
                        if (c == ':') 
                        {
                            std::cout << current_part << "\n";//Когда встречаем двоеточие - выводим накопленную часть
                            current_part = "";//Сбрасываем временную строку для следующей части
                        }
                        else 
                        {
                            current_part += c;//Иначе добавляем символ к строке

                        }
                    }
                    std::cout << current_part << "\n";//Выводим последнюю часть (после последнего двоеточия)
                }
                else 
                { 
                    std::cout << valueStr << "\n";//Если двоеточий нет - просто выводим значение как есть
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
        //Создаем процесс и записываем process id
        pid_t pid = fork();
        
        //Если дочерний процесс(в нем выполним бинарник)
        if (pid == 0) 
        {
            // Создаем копии строк для аргументов
            std::vector<std::string> tokens;
            //Указатели для execvp
            std::vector<char*> args;
            std::string token;
            //Разбиваем по пробелам для аргументов
            std::istringstream iss(input);
            
            while (iss >> token) 
            {
                tokens.push_back(token);  // Сохраняем копии
            }
            
            // Преобразуем в char*
            for (auto& t : tokens) 
            {
                args.push_back(const_cast<char*>(t.c_str()));
            }
            //Для execvp чтобы видел конец
            args.push_back(nullptr);
            
            //Заменяем программу на новую
            //args[0] - название команды, args.data() - ссылка на C массив строк для аргументов
            execvp(args[0], args.data());
            
            //Если не нашли команду то выведет это(вернет управление), при успехе не дойдем до этих строк
            std::cout << args[0] << ": command not found\n";
            exit(1);
            
        } 
        else if (pid > 0) 
        {
            int status;
            //Ожидаем дочерний
            waitpid(pid, &status, 0);
        } 
        else 
        {
            std::cerr << "Failed to create process\n";
        }
    }
        std::cout<<"$ ";
    }
}
