#include "json.hpp"
#include <iostream>
#include <fstream>
#include <string>

using json = nlohmann::json;

int main() {
	json j;
	std::string s= "[[\"hello\", \"world\"], [\"go\", \"go\"]]";
	j =json::parse(s);
	std::string name = j[0][0];
	std::cout<<j.size()<<std::endl;
	std::cout<<name<<" "<<j[0][1]<<std::endl;
	json k;
	k[0] = "hello";
	k[1] = "world";
	j.push_back(k);
	std::cout<<j<<std::endl;
}
