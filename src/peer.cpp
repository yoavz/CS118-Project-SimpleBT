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
#include "util/hash.hpp"

namespace sbt {

  Peer::Peer (const std::string& peerId,
        const std::string& ip,
        uint16_t port)
  : m_peerId(peerId)
  , m_ip(ip)
  , m_port(port)
  , interested(false) 
  , requested(false) 
  , unchoked(false) 
  , unchoking(false) 
{
}

  Peer::Peer (const std::string& peerId,
        const std::string& ip,
        uint16_t port,
        std::vector<bool>* clientPiecesDone,
        std::vector<bool>* clientPiecesLocked,
        FILE *clientFile)
  : m_peerId(peerId)
  , m_ip(ip)
  , m_port(port)
  , interested(false) 
  , requested(false) 
  , unchoked(false) 
  , unchoking(false) 
  , m_clientPiecesDone(clientPiecesDone)
  , m_clientPiecesLocked(clientPiecesLocked)
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

  // hand off to the main running function
  run();
}

void
Peer::run()
{
  // TODO: when should we close with this peer?
  while (true) 
  {
    // if we are not waiting on unchoke or piece already 
    if (!interested && !requested)
    {
      // if they have a piece we want
      int pieceIndex = getFirstAvailablePiece();
      if (pieceIndex >= 0) {
        log("found piece: " + std::to_string(pieceIndex));
        // if we are choked, send a interested msg
        if (!unchoked) {
          msg::Interested interest;
          ConstBufferPtr cbf = interest.encode();
          send(m_sock, cbf->buf(), cbf->size(), 0);

          interested = true;
          log("Sent interested message"); 
        }
        // if not choked, send the request
        else {
          int pieceLength = m_metaInfo->getPieceLength(); 
          // if it's the final piece, it's a diff length
          if (pieceIndex == m_metaInfo->getNumPieces()-1) {
            pieceLength = m_metaInfo->getLength() % m_metaInfo->getPieceLength();
            if (pieceLength == 0)
              pieceLength = m_metaInfo->getPieceLength();
          }

          msg::Request req(pieceIndex, 0, pieceLength); 
          ConstBufferPtr cbf = req.encode();
          send(m_sock, cbf->buf(), cbf->size(), 0);

          requested = true;
          log("Send request message");
        }
      } else {
        log("did not find a piece");
      }
    }

    waitOnMessage();

  }
}

// Finds the first available piece to download. If none
// are found, returns -1
// Locks the available piece
// TODO: warning critical section
int
Peer::getFirstAvailablePiece()
{
  for (int i=0; i<m_metaInfo->getNumPieces(); i++) 
  {
    if (!m_clientPiecesDone->at(i) && m_piecesDone.at(i)) {
      return i;
    }
  }

  return -1;
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

  // uint32_t length = ntohl(*reinterpret_cast<uint32_t *> (bfBuf));

  // std::cout << length << std::endl;
  // next byte is the ID 
  // uint8_t id = *(bfBuf+4);
  //
  // std::cout << "length: " << length << std::endl;
  // std::cout << "is bitfield: " << (id == msg::MSG_ID_BITFIELD) << std::endl;

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
    log("recv error");
    perror("recv");
    return -1;
  }

  // first 4 bytes are the length
  uint32_t length = ntohl(*reinterpret_cast<uint32_t *> (msgBuf));
  uint32_t msgLength = length+4;

  // std::cout << length << std::endl;
  // next byte is the ID 
  uint8_t id = *(msgBuf+4);

  // if there is a body to recieve, wait for it
  if (length > 1) {
    // reallocate the msgBuf to the proper length
    msgBuf = (char *)realloc(msgBuf, msgLength);
    if ((status = recv(m_sock, msgBuf+5, msgLength-5, 0)) == -1) {
      log("recv error");
      perror("recv");
      return -1;
    }
  }

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
    case msg::MSG_ID_KEEP_ALIVE:
      log("Recieved keep alive message");
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
  char *bitfield = (char *)bf->buf();

  uint32_t a = *(reinterpret_cast<uint32_t *> (bitfield));
  std::cout << "bitfield as int: " << a << std::endl;
  std::cout << "size of bitfield: " << size << std::endl;

  for (int count=0; count < size; count++) {
    // std::cout << "checking bit: " << size-count-1 << std::endl;
    if (*bitfield & (1 << (size-count-1))) {
      m_piecesDone[count] = true;
      std::cout << "piece " << count << " done"<<std::endl;
    } else {
      m_piecesDone[count] = false;
      // std::cout << "piece " << count << " needed"<<std::endl;
    }
  }

  // std::cout << "size of bitfield: " << m_piecesDone.size() << std::endl;
}

void 
Peer::log(std::string msg)
{
  std::cout << "(" << m_peerId << "): " << msg << std::endl;
  return;
}

void Peer::handleUnchoke(ConstBufferPtr cbf)
{
  log("recieved unchoke");

  unchoked = true;
  interested = false;
  return;
}

void Peer::handleInterested(ConstBufferPtr cbf)
{
  //TODO:
  return;
}

void Peer::handleHave(ConstBufferPtr cbf)
{
  log("recieved have");

  msg::Have have;
  have.decode(cbf);

  // set the piece 
  m_piecesDone[have.getIndex()] = true;
  return;
}


void Peer::handleBitfield(ConstBufferPtr cbf)
{
  log("Recieved bitfield out of order");
  //pthread_exit(NULL);
  return;
}

void Peer::handleRequest(ConstBufferPtr cbf)
{
  // TODO:
  if (unchoking) {

  }
  return;
}

void Peer::handlePiece(ConstBufferPtr cbf)
{

  msg::Piece piece;
  piece.decode(cbf);

  log("recieved piece " + std::to_string(piece.getIndex()) + " length: " + std::to_string(piece.getBlock()->size()));
  ConstBufferPtr pieceSha1 = util::sha1(piece.getBlock());
  
  if (!equal(pieceSha1, m_metaInfo->getHashOfPiece(piece.getIndex()))) {
    log("difference in hash");
  } else {
    log("same hash");
    sendHave(m_sock, piece.getIndex());
    log("sent have");
  }

  requested = false;

  //TODO: check if we have the file: if not, write it yo!

  //TODO: warning critical section
  (*m_clientPiecesDone)[piece.getIndex()] = true;

  //TODO: send have 

  return;
}

// input: a socket to send to and a piece index
// sends a "have" message 
void
Peer::sendHave(int sock, int pieceIndex)
{
  msg::Have have(pieceIndex);
  ConstBufferPtr cbf = have.encode();
  send(sock, cbf->buf(), cbf->size(), 0);
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
