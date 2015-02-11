// #include <string.h>
// #include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>

#include "peer.hpp"
#include "msg/msg-base.hpp"

namespace sbt {

Peer::Peer (const std::string& peerId,
            const std::string& ip,
            uint16_t port)
  : m_peerId(peerId)
  , m_ip(ip)
  , m_port(port)
{
}

void
Peer::handshakeAndRun()
{
  return;
}

int
Peer::waitOnMessage()
{
  int status;

  // first, we wait on a length and ID, which will always be 5 bytes
  char *msgBuf = (char *) malloc (5);
  if ((status = recv(m_sock, msgBuf, 5, 0)) == -1) {
    perror("recv");
    return -1;
  }

  // first 4 bytes are the length
  uint32_t length = ntohl(*reinterpret_cast<uint32_t *> (msgBuf));
  uint32_t msgLength = length+4;

  // std::cout << length << std::endl;
  // next byte is the ID 
  uint8_t id = *(msgBuf+4);

  // reallocate the msgBuf to the proper length
  msgBuf = (char *)realloc(msgBuf, msgLength);
  if ((status = recv(m_sock, msgBuf+5, msgLength-5, 0)) == -1) {
    perror("recv");
    return -1;
  }

  // generate a ConstBufferPtr
  ConstBufferPtr cbf = std::make_shared<Buffer> (msgBuf, msgLength);

  switch (id) {
    case msg::MSG_ID_UNCHOKE:
      handleUnchoke(cbf);
      break;
    case msg::MSG_ID_INTERESTED:
      handleInterested(cbf);
      break;
    case msg::MSG_ID_HAVE:
      handleHave(cbf);
      break;
    case msg::MSG_ID_BITFIELD:
      handleBitfield(cbf);
      break;
    case msg::MSG_ID_REQUEST:
      handleRequest(cbf);
      break;
    case msg::MSG_ID_PIECE:
      handlePiece(cbf);
      break;
    default:
      log("Recieved unknown message, not doing anything");
      break;
  }

  // msg::Bitfield bf;
  // bf.decode(bf_buf);
  //
  // if (bf.getId() == msg::MSG_ID_BITFIELD)
  //   std::cout << "good" << std::endl; 

  return 0;
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

void 
Peer::log(std::string msg)
{
  std::cout << "(PEER " << m_peerId << "): " << msg << std::endl;
  return;
}

void Peer::handleUnchoke(ConstBufferPtr cbf)
{
  return;
}

void Peer::handleInterested(ConstBufferPtr cbf)
{
  return;
}

void Peer::handleHave(ConstBufferPtr cbf)
{
  return;
}


void Peer::handleBitfield(ConstBufferPtr cbf)
{
  return;
}

void Peer::handleRequest(ConstBufferPtr cbf)
{
  return;
}

void Peer::handlePiece(ConstBufferPtr cbf)
{
  return;
}

} // namespace sbt
