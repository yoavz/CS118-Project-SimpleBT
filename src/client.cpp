// for file input 
#include <fstream> 

// networking 
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// internal references
#include "client.hpp"
#include "http/url-parsing.hpp"
#include "http/http-request.hpp"

namespace sbt {

  Client::Client(const std::string& port, const std::string& torrent) {
    // for now, set debug flag
    this->debug = true;

    this->torrentFile = torrent;
    this->port = std::stoi(port); 

    // parse the torrent file into this->metaInfo
    this->parseTorrentFile();

  }

  int Client::parseTorrentFile() {
    std::ifstream ifs;
    ifs.open(this->torrentFile, std::ifstream::in);
    this->metaInfo.wireDecode(ifs);

    return 0;
  }


  int Client::fetchPeerList() {
    
    std::string announceString = metaInfo.getAnnounce();
    std::cout << announceString << std::endl;
    Url announce (announceString) ;

    HttpRequest req;
    req.setHost(announce.getHost());
    req.setPort(announce.getPort());
    req.setMethod(HttpRequest::GET);
    req.setPath(announce.getPath());
    req.setVersion("1.1");
    req.addHeader("Accept-Language", "en-US");
    
    // get IP address
    struct addrinfo hints;
    // prepare hints
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res;
    int status;
    if ((status = getaddrinfo(announce.getHost().c_str(), 
                              announce.getPortString().c_str(), 
                              &hints, &res)) != 0)
    {
      std::cerr << "getaddrinfo: " << gai_strerror(status) << std::endl;
      return -1;
    }

    struct sockaddr_in* serverAddr = (struct sockaddr_in*)res->ai_addr;

    // initialize and connect the socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, 
                (struct sockaddr *)serverAddr, 
                sizeof(*serverAddr)) == -1) {
      perror("connect");
      return 2;
    }

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    if (getsockname(sockfd, (struct sockaddr *)&clientAddr, &clientAddrLen) == -1) {
      perror("getsockname");
      return 3;
    }

    char ipstr[INET_ADDRSTRLEN] = {'\0'};
    inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
    std::cout << "Set up a connection from: " << ipstr << ":" <<
      ntohs(clientAddr.sin_port) << std::endl;
     

    // size_t reqLen = req.getTotalLength();
    // char *buf = new char [reqLen];
    //
    // req.formatRequest(buf);
    // std::cout << buf;

    return 0;
  }
}
