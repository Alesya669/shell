#include <iostream>

#include <string>
using namespace std;
int main()
 {

string input;
while(getline(cin,input))
{
	if(input=="\\q")
	{
		break;
	}
cout<<input<<endl;
}
return 0;
}
