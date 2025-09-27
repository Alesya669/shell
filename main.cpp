#include <iostream>

#include <string>
using namespace std;
int main() {
string input;
while(true)
{
	if(!getline(cin,input))
	{
		break;
	}
cout<<input<<endl;
}
return 0;
}
