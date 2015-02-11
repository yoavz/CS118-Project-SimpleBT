// #include <string.h>
// #include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>

#include "peer.hpp"
#include "msg/handshake.hpp"
#include "util/buffer-stream.hpp"

namespace sbt {

  Peer::Peer (const std::string& peerId,
        const std::string& ip,
        uint16_t port)
  : m_peerId(peerId)
  , m_ip(ip)
  , m_port(port)
{
}

  Peer::Peer (const std::string& peerId,
        const std::string& ip,
        uint16_t port,
        std::vector<bool>* clientPiecesDone,
        FILE *clientFile)
  : m_peerId(peerId)
  , m_ip(ip)
  , m_port(port)
  , m_clientPiecesDone(clientPiecesDone)
  , m_clientFile(clientFile)
{
}

void
Peer::handshakeAndRun()
{
  log("Attempting to connect...");

  // generate the socket and connect it
  int peerSock = socket(AF_INET, SOCK_STREAM, 0);
  setSock(peerSock);
  if (connectSocket()) {
    // pthread_exit(NULL);
    return;
  } else {
    log("Connection successful");
  }

  // send our handshake
  msg::HandShake hs(m_metaInfo->getHash(), "SIMPLEBT-TEST-PEERID");
  ConstBufferPtr hsMsg = hs.encode();
  send(m_sock, hsMsg->buf(), hsMsg->size(), 0);

  // wait for a handshake
  if (waitOnHandshake()) {
    // pthread_exit(NULL);
    return;
  } else {
    log("handshake exchange successfull");
  }

  // construct and send our bitfield 
  msg::Bitfield bf = constructBitfield();
  ConstBufferPtr bfMsg = bf.encode(); 
  send(m_sock, bfMsg->buf(), bfMsg->size(), 0);

  // wait on the bitfield (this fctn also parses the bitfield)
  if (waitOnBitfield(bfMsg->size())) {
    // pthread_exit(NULL);
    return;
  } else {
    log("bitfield exchange successfull");
  }

  return;
}

int
Peer::connectSocket() 
{
  struct addrinfo hints;
  struct addrinfo* res;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; // IPv4
  hints.ai_socktype = SOCK_STREAM;

  std::string peerPort = std::to_string(getPort());

  // get address
  int status = 0;
  if ((status = getaddrinfo(getIp().c_str(), peerPort.c_str(), &hints, &res)) != 0) 
  {
    log("Could not find addrinfo for IP");
    return -1;
  }

  struct sockaddr_in* ipv4 = (struct sockaddr_in*)res->ai_addr;
  char ipstr[INET_ADDRSTRLEN] = {'\0'};
  inet_ntop(res->ai_family, &(ipv4->sin_addr), ipstr, sizeof(ipstr));

  if (connect(getSock(), res->ai_addr, res->ai_addrlen) == -1) {
    perror("connect");
    return -1;
  }

  freeaddrinfo(res);

  return 0;
}

// Waits on a handshake, returns 0 if recieved a successful handshake
// returns -1 if the handshake has the wrong hash or error
int
Peer::waitOnHandshake()
{
  int HANDSHAKE_LENGTH = 68;
  int status;

  // handshake is always length 68
  char *hsBuf = (char *) malloc (HANDSHAKE_LENGTH);
  if ((status = recv(m_sock, hsBuf, HANDSHAKE_LENGTH, 0)) == -1) {
    perror("recv");
    return -1;
  }

  ConstBufferPtr cbf = std::make_shared<Buffer> (hsBuf, HANDSHAKE_LENGTH);
  msg::HandShake hs;
  hs.decode(cbf);

  // update the peer ID
  setPeerId(hs.getPeerId());

  // check the info hashes match. 
  if (memcmp(m_metaInfo->getHash()->buf(), 
             hs.getInfoHash()->buf(), 
             20) != 0) {
    log("detected incorrect hash on handshake");
    // pthread_exit(NULL);
    return -1;
  }

  return 0;
}

// Input: size- the size of the Bitfield msg we are expecting,
// INLCLUDING length/id of the msg 
// Waits on a bitfield, returns 0 if recieved a successful 
// returns -1 if the bitfield is the wrong length or error
// This function also calls setBitfield, which parses the
// bitfield in m_piecesDone
int
Peer::waitOnBitfield(int size)
{
  int status;

  char *bfBuf = (char *) malloc (size);
  if ((status = recv(m_sock, bfBuf, size, 0)) == -1) {
    perror("recv");
    return -1;
  }

  ConstBufferPtr cbf = std::make_shared<Buffer> (bfBuf, size);
  msg::Bitfield bf;
  bf.decode(cbf);

  // parse/set the bitfield
  setBitfield(bf.getBitfield(), m_metaInfo->getNumPieces());

  return 0;
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

  return 0;
}

// input: a bitfield in cbf form, 
//        size: the number of BITS in the bf
//        NOTE that this is not the amount of bytes
//        in the bitfield nor the amount of bytes
//        of the bitfield message
// parses a bitfield into m_piecesDone
void
Peer::setBitfield(ConstBufferPtr bf, int size)
{
  m_piecesDone = std::vector<bool> (size);
  const char *bitfield = (const char *)bf->buf();

  for (int count=0; count < size; count++) {
    if (*bitfield & (1 << (size-count))) {
      m_piecesDone.at(count) = true;
    } else {
      m_piecesDone.at(count) = false;
    }
  }
}

void 
Peer::log(std::string msg)
{
  std::cout << "(" << m_peerId << "): " << msg << std::endl;
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
  log("recieved bitfield");

  msg::Bitfield bf;
  bf.decode(cbf);

  ConstBufferPtr bitfield = bf.getBitfield();
  return;
}

void Peer::handleRequest(ConstBufferPtr cbf)
{
  return;
}

void Peer::handlePiece(ConstBufferPtr cbf)
{
  log("recieved piece");

  msg::Piece piece;
  piece.decode(cbf);

  return;
}

// constructs a bitfield based on the client's current files
// TODO: WARNING critical section

msg::Bitfield
Peer::constructBitfield()
{

  // construct a bitfield
  int64_t fileLength = m_metaInfo->getLength();
  int64_t pieceLength = m_metaInfo->getPieceLength();
  int numPieces = fileLength / pieceLength + (fileLength % pieceLength == 0 ? 0 : 1);
  int numBytes = numPieces/8 + (numPieces%8 == 0 ? 0 : 1);

  char *bitfield = (char *) malloc(numBytes);
  memset(bitfield, 0, numBytes);

  int byteNum, bitNum;
  for (int count=0; count < numPieces; count++)
  {
    byteNum = count / 8;    
    bitNum = count % 8;

    if (m_clientPiecesDone->at(count)) {
      *(bitfield+byteNum) |= 1 << (7-bitNum);
    } 
  }

  OBufferStream bfstream;
  bfstream.write(bitfield, numBytes);
  msg::Bitfield bf_struct(bfstream.buf());

  return bf_struct;
}

} // namespace sbt
