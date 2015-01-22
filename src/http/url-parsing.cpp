#include <iostream>

#include "url-parsing.hpp"

namespace sbt {

  Url::Url(const std::string &base) {
    std::vector<std::string> temp;
    temp = extract(base, "://");
    this->_protocol = temp.at(0);
    std::string hostAndPath = temp.at(1);

    temp = extract(hostAndPath, ":");
    this->_host = temp.at(0);
    std::string portAndPath = temp.at(1);

    temp = extract(portAndPath, "/");
    this->_portString = temp.at(0);
    this->_port = std::stoi(this->_portString);
    this->_path = "/" + temp.at(1);
  }

  std::string Url::getProtocol() {
    return this->_protocol;
  }

  std::string Url::getHost() {
    return this->_host;
  }

  std::string Url::getPath() {
    return this->_path;
  }

  std::string Url::getPortString() {
    return this->_portString;
  }

  int Url::getPort() {
    return this->_port;
  }

  int Url::setParam(std::string key, std::string value)
  {
    this->params[key] = value;
    return 0;
  }

  int Url::removeParam(std::string key)
  {
    this->params.erase(key);
    return 0;
  }

  std::string Url::serializePath()
  {
    std::string path = this->getPath();

    for (auto it = this->params.begin(); it != this->params.end(); ++it) {
      path += ( it == this->params.begin() ? "?" : "&" );
      path += it->first + "=" + it->second;
    }

    return path;
  }

  /*
   * Helper function that splits a base string by a string delimiter
   * If the delimiter is not found, it throws a UrlParsingException
   * For example: extract("https://localhost:12345/path", "://")
   *              => [ "https", "localhost:12345/path" ]
   */
  std::vector<std::string> Url::extract(const std::string& base, const std::string& delim) {

    std::vector<std::string> split;

    int pos;
    if ((pos = base.find(delim)) != std::string::npos) {
      split.push_back(base.substr(0, pos));
      split.push_back(base.substr(pos + delim.length(), base.length()));
    } else {
      throw 10;
      // throw UrlParsingException;
    }

    return split;
  }

}
