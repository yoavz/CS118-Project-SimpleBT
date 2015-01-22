// for file input 
#include <fstream> 
#include <sstream>

// networking 
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// internal references
#include "client.hpp"
#include "tracker-response.hpp"
#include "http/url-parsing.hpp"
#include "http/http-request.hpp"
#include "http/http-response.hpp"
#include "http/http-headers.hpp"
#include "http/url-encoding.hpp"
#include "util/bencoding.hpp"
#include "msg/handshake.hpp"

namespace sbt {

  int extract(const std::string& base, 
              const std::string& delim,
              std::vector<std::string>& v) { 
    int pos;
    if ((pos = base.find(delim)) != std::string::npos) {
      v.push_back(base.substr(0, pos));
      v.push_back(base.substr(pos + delim.length(), base.length()));
    } else {
      return 1;
    }

    return 0;
  }

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


  int Client::trackerRequest() {
    
    std::string announceString = metaInfo.getAnnounce();
    Url announce (announceString) ;
    
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
    int sockfd = this->setUpSocket(serverAddr);

    // encode all request params
    ConstBufferPtr hash = metaInfo.getHash();
    const uint8_t* hashBuffer = hash->buf();
    std::string hashEncoded = url::encode(hashBuffer, 20);
    if (this->debug) {
      std::cout << "info hash encoded: " << hashEncoded << std::endl;
    }
    announce.setParam("info_hash", hashEncoded); 
    announce.setParam("port", std::to_string(this->port));
    announce.setParam("peer_id", "abcdefghijklmnopqrst");
    announce.setParam("uploaded", "0");
    announce.setParam("downloaded", "0");
    announce.setParam("left", "0");
    announce.setParam("event", "started");

    // serialize the HTTP request
    HttpRequest req;
    req.setHost(announce.getHost());
    req.setPort(announce.getPort());
    req.setMethod(HttpRequest::GET);
    req.setPath(announce.serializePath());
    req.setVersion("1.1");
    req.addHeader("Accept-Language", "en-US");
    size_t reqLen = req.getTotalLength();
    char *buf = new char [reqLen];
    req.formatRequest(buf);


    if (this->debug) {
      std::cout << "Sending request to server..." << std::endl;
      std::cout << buf;
    }

    // send to socket
    if (send(sockfd, buf, reqLen, 0) == -1) {
      perror("send");
      return 4;
    }

    // read the response into a stream
    std::stringstream respStream;
    if (waitForResponse(sockfd, respStream))
      return 5;

    // extract the HTTP body from the message
    TrackerResponse trackResp;
    if (this->parseIntoTrackerResp(respStream.str(), trackResp)) {
      std::cerr << "response parse error (response printed below" << std::endl;
      std::cerr << respStream.str() ;
    }
    respStream.str(""); // flush the stream

    std::vector<PeerInfo> peers = trackResp.getPeers();
    for(std::vector<PeerInfo>::iterator it = peers.begin(); it != peers.end(); ++it) 
    {
      std::cout << it->ip << ":" << it->port << std::endl;
    }
    return 0;

    // announce.removeParam("event");
    // req.setPath(announce.serializePath());
    // reqLen = req.getTotalLength();
    // buf = new char [reqLen];
    // req.formatRequest(buf);

    uint64_t interval;
    int counter = 0;
    interval = trackResp.getInterval();
    if (this->debug) {
      std::cout << "Waiting " << interval << " seconds..." << std::endl;
    }
    while (sleep(interval) == 0 || counter > 100) {
      if (this->debug) {
        std::cout << "Sending " << ++counter << "th request to server..." << std::endl;
        std::cout << buf;
      }

      // set up a new socket
      sockfd = this->setUpSocket(serverAddr);

      // send to socket
      if (send(sockfd, buf, reqLen, 0) == -1) {
        perror("send");
        return 4;
      }

      // read into stream
      std::stringstream newStream;
      if (waitForResponse(sockfd, newStream))
        return 5;

      if (this->debug) {
        std::cout << "Got response from server" << std::endl;
        std::cout << newStream.str();
      }

      TrackerResponse trackerResp;
      if (this->parseIntoTrackerResp(newStream.str(), trackResp)) {
        std::cerr << "response parse error (response printed below)" << std::endl;
        std::cerr << newStream.str() << std::endl;
        return 1;
      }

      interval = trackResp.getInterval();
      if (this->debug) {
        std::cout << "Waiting " << interval << " seconds..." << std::endl;
      }
    }

    return 0;
  }

  int Client::waitForResponse(int sockfd, std::stringstream& ss)
  {
    char *respBuf = new char [20];
    bool isEnd = false;
    int status;

    while (!isEnd) {

      if ((status = recv(sockfd, respBuf, 20, 0)) == -1) {
        perror("recv");
        return 5;
      }

      // recv returns 0 on EOF
      if (status == 0)
        isEnd = true;

      // if (this->debug) {
      //   std::cout << "Recieved chunk: " << respBuf << std::endl;
      //   std::cout << "isEnd: " << isEnd << std::endl;
      // }

      ss << respBuf;
    }
    
    return 0;
  }

  int Client::parseIntoTrackerResp(std::string s, TrackerResponse& tr) {
    // extract the HTTP body from the message
    std::vector<std::string> temp;
    if (extract(s, "\r\n", temp)) 
      return 1;
    std::string httpRest = temp.at(1);
    temp.clear();
    if (extract(httpRest, "\r\n\r\n", temp))
      return 1;
    std::string bencodedBody = temp.at(1);
    temp.clear();

    if (this->debug) {
      std::cout << "Beconded body: " << bencodedBody << std::endl;
    }

    bencoding::Dictionary ben;
    std::istringstream bencodedBodyStream(bencodedBody);
    ben.wireDecode(bencodedBodyStream);

    tr.decode(ben);

    return 0;
  }

  int Client::setUpSocket(struct sockaddr_in *serverAddr) {

    // initialize and connect the socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, 
                (struct sockaddr *)serverAddr, 
                sizeof(*serverAddr)) == -1) {
      perror("connect");
      return -1;
    }

    // get local socket information
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    if (getsockname(sockfd, (struct sockaddr *)&clientAddr, &clientAddrLen) == -1) {
      perror("getsockname");
      return 3;
    }
    char ipstr[INET_ADDRSTRLEN] = {'\0'};
    inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
    unsigned short clientPort = clientAddr.sin_port;
    if (this->debug) {
      std::cout << "Set up a connection from: " << ipstr << ":" <<
        clientPort << std::endl;
    }

    return sockfd;
  }

}
