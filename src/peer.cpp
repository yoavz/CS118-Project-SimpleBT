// #include <string.h>
// #include <stdio.h>

#include "peer.hpp"

namespace sbt {

Peer::Peer (const std::string& peerId,
            const std::string& ip,
            uint16_t port)
  : m_ip(ip)
  , m_port(port)
  , m_peerId(peerId)
  , m_choked(false)
  , m_interested(false)
{

}

void
Peer::run()
{
  return;
}

} // namespace sbt
