#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

int main() 
{
    vector<string> history;
    string input;
    bool running = true;
    string history_file = "kubsh_history.txt";
    ofstream write_file(history_file, ios::app);
    
    while (running && getline(cin, input)) 
    {
        if (input.empty()) {
            continue;
        }
        write_file << input << endl;
        write_file.flush();
        
        if (input == "\\q") 
        {
            running = false;
            break;
        }
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
        else if (input[0] == '/')  // Выполнение бинарника
        {
            pid_t pid = fork(); // Создаём дочерний процесс
            if (pid == 0) {
                // Дочерний процесс
                execl(input.c_str(), input.c_str(), NULL); // Выполняем бинарник
                perror("Ошибка выполнения"); // Если exec не удался
                exit(1); // Завершаем процесс с ошибкой
            } else if (pid > 0) {
                // Родительский процесс
                wait(NULL); // Ожидаем завершения дочернего процесса
            } else {
                perror("Ошибка fork");
            }
            history.push_back(input);
        }
        else 
        {
            cout << input << ": command not found" << endl;
            history.push_back(input);
        }
    }
    
    write_file.close();
    return 0;
}
