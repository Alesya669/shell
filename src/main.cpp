x#include <iostream>
#include <string>
#include <vector>
#include <fstream>

using namespace std;

int main() 
{
    vector<string> history;
    string input;
    bool running = true;
    
    while (running && getline(cin, input)) 
    {
        if (input.empty()) {
            continue;
        }
        
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
        else 
        {
            
            cout << input << ": command not found" << endl;
            history.push_back(input);
        }
    }
    
    return 0;
}
