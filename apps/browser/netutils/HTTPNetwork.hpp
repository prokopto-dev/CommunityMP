#ifndef NEWLAUNCHER_HTTPNETWORK_HPP
#define NEWLAUNCHER_HTTPNETWORK_HPP


#include <string>

class HTTPNetwork
{
public:
    HTTPNetwork(std::string addr, unsigned short port);
    ~HTTPNetwork();
    std::string getData(const char *uri);
    std::string getDataPOST(const char *uri, const char* body,  const char* contentType = "application/json");
    std::string getDataPUT(const char *uri, const char* body, const char* contentType = "application/json");

protected:
    std::string address;
    unsigned short port;
};


#endif //NEWLAUNCHER_HTTPNETWORK_HPP
