// #include <string.h>
// #include <stdio.h>

#include <stdio.h>
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

void
Peer::setBitfield(ConstBufferPtr bf, int size)
{
  m_piecesDone = std::vector<bool> (size);
  const char *bitfield = (const char *)bf->buf();

  for (int count=0; count < size; count++) {
    if (*bitfield & (1 << (size-count))) {
      // std::cout << "detected piece: " << count << std::endl;
      m_piecesDone.at(count) = true;
    } else {
      m_piecesDone.at(count) = false;
    }
  }
}

} // namespace sbt
