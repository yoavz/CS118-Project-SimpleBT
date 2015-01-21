#ifndef SBT_URL_PARSING_HPP 
#define SBT_URL_PARSING_HPP 

#include <exception>
#include <string> 
#include <vector>

namespace sbt {

class Url
{
public:

  // Default constructor
  Url(const std::string &base);

  // getters
  std::string getProtocol();
  std::string getHost();
  std::string getPath();
  std::string getPortString();
  int getPort();

private:

  int _port;
  std::string _base, _protocol, _host, _path, _portString;

  std::vector<std::string> extract(const std::string& base, const std::string& delim);
};

/*
 * The exception that is thrown when there is an error in parsing
 */
// class UrlParsingException: public std::exception
// {
//   virtual const char* what() const throw()
//   {
//     return "URL could not be parsed";
//   }
// } UrlParsingException;
//
}

#endif
