#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <cstdlib> // for getenv

using namespace std;

const char* home = getenv("HOME");
string historyPath = string(home) + "/.kubsh_history";
vector<string> history;
bool running = true;

void load_history() {
    ifstream file(historyPath);
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
    ofstream file(historyPath);
    if (!file.is_open()) return;
    for (const auto& cmd : history) {
        file << cmd << endl;
    }
    file.close();
}

void add_to_history(const string& command) {
    if (command.empty()) return;
    if (history.size() >= 100) {
        history.erase(history.begin());
    }
    history.push_back(command);
}

int main() {
    load_history();
    
    // Example usage
    add_to_history("kubectl get pods");
    add_to_history("kubectl apply -f deployment.yaml");
    
    save_history();
    
    cout << "History saved to: " << historyPath << endl;
    cout << "Commands in history: " << history.size() << endl;
    
    return 0;
}
