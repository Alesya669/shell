#include <algorithm>
#include <filesystem>
#include <csignal>
using namespace std;
class KubShell {
private:
    vector<string> history;
    string history_file;
    bool running;

    }

    void load_history() {
        ifstream file(history_file);
        if (!file.is_open()) return;

        string line;
        while (getline(file, line) && history.size() < 100) {
            if (!line.empty()) {
                history.push_back(line);
            }
        }
        file.close();
    }

    void save_history() {
        ofstream file(history_file);
        if (!file.is_open()) return;
for (const auto& cmd : history) { 
           file << cmd << endl; 
       } 
       file.close(); 
   } 

   void add_to_history(const string& command) { 
       if (command.empty() || command == "\\q") return; 
       if (!history.empty() && history.back() == command) return; 
       if (history.size() >= 100) { 
           history.erase(history.begin()); 
       } 
       history.push_back(command); 
   } 

   void echo(const string& args) { 
           cout << args << endl; 
   } 

   void execute_command(const string& command) { 
       if (command.empty()) return; 

       if (command == "\\q") { 
           cout << "Выход из kubsh" << std::endl; 
           running = false; 
           return; 
       } 
       if (command.find("echo") == 0) {
echo(command.substr(4));
            return;
        }
         cout << "Вы ввели: " << command << endl;
    }

public:
    KubShell() : running(true) {
        load_history();
        std::cout << "You are in kubsh! Use \\q for exit" << endl;
    }

    ~KubShell() {
        save_history();
    }

    void run() {
        string input;
        while (running) {
            cout << "kubsh> ";
            cout.flush();
            if (!getline(cin, input)) {
                cout << endl << "Bye!" << endl;
                break;
            }
            if (input.empty()) continue;
            if(input!="\\q") add_to_history(input);
execute_command(input);
        }
    }
};


int main() {
    KubShell shell;
    shell.run();
    return 0;
}
