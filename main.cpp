#include <iostream>
#include <string>

int main() {
    std::string input;
    
    while (true) {
        std::cout << "$ ";
        std::getline(std::cin, input);
        
        if (input.substr(0, 5) == "echo ") {
            std::string text = input.substr(5); 
            
            
            if (text.size() >= 2 && 
                ((text[0] == '"' && text[text.size()-1] == '"') ||
                 (text[0] == '\'' && text[text.size()-1] == '\''))) {
                text = text.substr(1, text.size() - 2);
            }
            
            std::cout << text << std::endl;
        }
       
        else if (input == "exit") {
            break;
        }
        
        else if (!input.empty()) {
            std::cout << "Necomanda: " << input << std::endl;
        }
    }
    
    return 0;
}
