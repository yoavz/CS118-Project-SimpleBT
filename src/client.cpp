// for file input 
#include <fstream> 
#include <string> 

// for string splitting
#include <boost/algorithm/string.hpp>

// internal references
#include "client.hpp"
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

    if (this->debug) {
      std::cout << "---------" << std::endl;
      std::cout << "Announce: " << metaInfo.getAnnounce() << std::endl ;
      std::cout << "File Name: " << metaInfo.getName() << std::endl ;
      std::cout << "File Length: "
                << std::to_string(metaInfo.getLength()) << std::endl ;
      std::cout << "Piece Length: " 
                << std::to_string(metaInfo.getPieceLength()) << std::endl ;
      std::cout << "---------" << std::endl;
    }

    return 0;
  }

  int Client::fetchPeerList() {
    //  * Example:
    //  *      // command line parsing
    //  *      HttpRequest req;
    //  *      req.setHost("www.google.com");
    //  *      req.setPort(80);
    //  *      req.setMethod(HttpRequest::GET);
    //  *      req.setPath("/");
    //  *      req.setVersion("1.0");
    //  *      req.addHeader("Accept-Language", "en-US");
    //  *
    //  *      size_t reqLen = req.getTotalLength();
    //  *      char *buf = new char [reqLen];
    //  *
    //  *      req.formatRequest(buf);
    //  *      cout << buf;
    //  *
    //  *      delete [] buf;
    //  */
    //
    // std::cerr << "Using port: " << port << std::endl ;
    //
    
    // split the announce into host and port
    // std::vector<std::string> strs;
    // std::string announce = metaInfo.getAnnounce();
    // boost::split(strs, announce, boost::is_any_of(":"));
    //
    // if (strs.size() != 2) {
    //   std::cerr << "Malformed announce: " << announce << std::endl ;
    //   return -1;
    // }
    //
    // std::cerr << strs.at(0) << strs.at(1) << std::endl;

    HttpRequest req;

    req.setHost(metaInfo.getAnnounce());
    req.setPort(80);
    req.setMethod(HttpRequest::GET);
    req.setPath("/");
    req.setVersion("1.1");
    req.addHeader("Accept-Language", "en-US");

    size_t reqLen = req.getTotalLength();
    char *buf = new char [reqLen];

    req.formatRequest(buf);
    std::cout << buf;

    return 0;
  }

}
