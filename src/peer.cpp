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
, m_activePiece(-1) 
, interested(false) 
, requested(false) 
, unchoked(false) 
, unchoking(false) 
{

}

Peer::Peer (int sockfd)
: m_sock(sockfd) 
, m_activePiece(-1) 
, interested(false) 
, requested(false) 
, unchoked(false) 
, unchoking(false) 
{
}

void 
Peer::setClientData(std::vector<bool>* clientPiecesDone,
                    std::vector<bool>* clientPiecesLocked,
                    MetaInfo *metaInfo,
                    std::vector<Peer>* peers,
                    FILE *clientFile,
                    pthread_mutex_t *clientPieceLock,
                    pthread_mutex_t *clientFileLock)
{
  m_clientPiecesDone = clientPiecesDone;
  m_clientPiecesLocked = clientPiecesLocked;
  m_metaInfo = metaInfo;
  m_peers = peers;
  m_clientFile = clientFile;
  pieceLock = clientPieceLock;
  pieceLock = clientFileLock;
}

// This function responds to the handshake of a 
// peer attempting to initiate a connection, 
// exchanges bitfields, and calls the main
// run() function of the Peer
void
Peer::respondAndRun()
{
  // wait for a handshake
  if (waitOnHandshake()) {
    // pthread_exit(NULL);
    return;
  } else {
    log("handshake exchange successfull");
  }

  // send our handshake
  msg::HandShake hs(m_metaInfo->getHash(), "SIMPLEBT-TEST-PEERID");
  ConstBufferPtr hsMsg = hs.encode();
  send(m_sock, hsMsg->buf(), hsMsg->size(), 0);

  // construct our bitfield (don't send it yet)
  // we use it's size to know how much bytes to 
  // wait for on their bitfield
  msg::Bitfield bf = constructBitfield();
  ConstBufferPtr bfMsg = bf.encode(); 

  // wait on the bitfield (this fctn also parses the bitfield)
  if (waitOnBitfield(bfMsg->size())) {
    // pthread_exit(NULL);
    return;
  } else {
    log("bitfield exchange successfull");
  }

  // send our bitfield
  send(m_sock, bfMsg->buf(), bfMsg->size(), 0);

  // run the main peer loop
  run();
}

// This function attempts to connect by initiating
// a handshake, exchanges bitfields, and calls the
// main run() function of the Peer
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
  while (true) 
  {
    // check if all pieces are done
    if (allPiecesDone()) {
      // log("all pieces done");
      //pthread_exit(NULL);
    } else {

      // if we are not waiting on unchoke or piece already 
      if (!interested && !requested)
      {
        // if we have not acquired a piece, try finding one
        if (m_activePiece < 0)
          getFirstAvailablePiece();
        
        if (m_activePiece >= 0) {
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
            if (m_activePiece == m_metaInfo->getNumPieces()-1) {
              pieceLength = m_metaInfo->getLength() % m_metaInfo->getPieceLength();
              if (pieceLength == 0)
                pieceLength = m_metaInfo->getPieceLength();
            }

            msg::Request req(m_activePiece, 0, pieceLength); 
            ConstBufferPtr cbf = req.encode();
            send(m_sock, cbf->buf(), cbf->size(), 0);

            requested = true;
            log("Send request message for piece: " + std::to_string(m_activePiece) + " with length: " + std::to_string(pieceLength));
          }
        } else {
          // no active piece found
        }
      }
    }

    waitOnMessage();
  }
}

// Finds the first available piece to download. If none
// are found, returns -1
// Locks the available piece
void
Peer::getFirstAvailablePiece()
{
  pthread_mutex_lock(pieceLock);
  for (int i=0; i<m_metaInfo->getNumPieces(); i++) 
  {
    if (!m_clientPiecesLocked->at(i) && m_piecesDone.at(i)) {
      (*m_clientPiecesLocked)[i] = true;
      m_activePiece = i;
      pthread_mutex_unlock(pieceLock);
      return;
    }
  }
  pthread_mutex_unlock(pieceLock);

  log("could not find piece from this peer");
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

  // length is first four bytes
  // uint32_t length = ntohl(*reinterpret_cast<uint32_t *> (bfBuf));
  // next byte is the ID 
  // uint8_t id = *(bfBuf+4);

  // next bytes are the bitfield
  char *bitfield = bfBuf+5;
  setBitfield(bitfield, m_metaInfo->getNumPieces());

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
      log("Unsupported: keep alive message");
      break;
    case msg::MSG_ID_CHOKE:
      // log("Unsupported: choke message");
      break;
    case msg::MSG_ID_NOT_INTERESTED:
      log("Unsupported: not interested message");
      break;
    case msg::MSG_ID_CANCEL:
      log("Unsupported: cancel message");
      break;
    case msg::MSG_ID_PORT:
      log("Unsupported: port message");
      break;
    default:
      log("Recieved unknown message, not doing anything");
      break;
  }

  return 0;
}

// input: a bitfield (highest bit is first piece), 
//        size: the number of BITS in the bf
//        NOTE that this is not the amount of bytes
//        in the bitfield nor the amount of bytes
//        of the bitfield message
// parses a bitfield into m_piecesDone
void
Peer::setBitfield(char *bitfield, int size)
{
  if (size > 32) {
    log("ERROR: can't yet handle bitfields of size > 32");
    m_piecesDone = std::vector<bool> (size);
    return;
  }

  m_piecesDone = std::vector<bool> (size);

  for (int i=0; i < size; i++) {
    if (*bitfield & (1 << (32-i))) {
      m_piecesDone[i] = true; 
    } else {
      m_piecesDone[i] = false;
    }
  }
  
  return;

  // OLD LOGGING CODE
  // // with a (full!) bitfield length of 23
  // // bitfield = 1111 1111 1111 1111 1111 111|X XXXX XXXX
  // // where X's are bits we don't care about
  // uint32_t a = *(reinterpret_cast<uint32_t *> (bitfield));
  //
  // std::cout << "bitfield: ";
  // for (int i=0; i<32; i++)
  //   std::cout << ((a >> (31-i)) & 1);
  // std::cout << std::endl;
  // std::cout << "bitfield (as uint32_t): " << a << std::endl;
  //
  // // a >> 9 = 0000 0000 0|111 1111 1111 1111 1111 1111
  // uint32_t b = a >> (32-size);
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
  log("recieved interested");

  msg::Unchoke unchoke;
  ConstBufferPtr resp = unchoke.encode();
  send(m_sock, resp->buf(), resp->size(), 0);

  unchoking = true;

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

  msg::Request req;
  req.decode(cbf);

  int index = req.getIndex();
  int begin = req.getBegin();
  int length = req.getLength();

  // TODO: sanity checks that above are valid?

  log("recieved request with index: " + std::to_string(index) +
      ", begin: " + std::to_string(begin) + ", length: " +
      std::to_string(length));

  if (unchoking) {
    // read from file
    char *rBuf = (char *) malloc (sizeof(char)*length);

    if (!m_clientFile) {
      log("ERROR: file pointer closed");
      return;
    }

    // critical section: seeking/writing to file
    pthread_mutex_lock(fileLock);
    fseek(m_clientFile, index * m_metaInfo->getPieceLength() + begin, SEEK_SET);
    if (fread(rBuf, 1, length, m_clientFile) != length) {
      log("fread error");
      pthread_mutex_unlock(fileLock);
      return;
    }
    pthread_mutex_unlock(fileLock);

    // send off the piece
    ConstBufferPtr block = std::make_shared<const Buffer> (rBuf, length);
    msg::Piece piece(index, begin, block);
    ConstBufferPtr resp = piece.encode();
    send(m_sock, resp->buf(), resp->size(), 0);
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
    //TODO: check if we have the file?

    //write to file
    if (writeToFile(piece.getIndex(), piece.getBlock())) {
      log("Problem writing to file");
    } else {
      log("Successfully wrote to file");
      pthread_mutex_lock(pieceLock);
      (*m_clientPiecesDone)[piece.getIndex()] = true;
      (*m_clientPiecesLocked)[piece.getIndex()] = true; // just in case
      pthread_mutex_unlock(pieceLock);

      m_activePiece = -1;
    }

    // send have to all peers
    for (auto& peer : *m_peers) {
      peer.sendHave(piece.getIndex());
      log("sent have to " + peer.getPeerId());
    }

  }

  requested = false;

  return;
}

// sends a "have" message to this peer with the pieceIndex
void
Peer::sendHave(int pieceIndex)
{
  msg::Have have(pieceIndex);
  ConstBufferPtr cbf = have.encode();
  send(m_sock, cbf->buf(), cbf->size(), 0);
  return; 
}

// constructs a bitfield based on the client's current files
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

// writes the piece index to the file
int
Peer::writeToFile(int pieceIndex, ConstBufferPtr piece)
{
  if (!m_clientFile) {
    log("File pointer not open, returning");
    return -1;
  }

  // sanity check: piece length
  int pieceLength = m_metaInfo->getPieceLength(); 
  // if it's the final piece, it's a diff length
  if (pieceIndex == m_metaInfo->getNumPieces()-1) {
    pieceLength = m_metaInfo->getLength() % m_metaInfo->getPieceLength();
    if (pieceLength == 0)
      pieceLength = m_metaInfo->getPieceLength();
  }

  if (piece->size() != pieceLength) {
    log("Incorrect piece length in writeToFile");
    return -1;
  }

  int piecePosStart = pieceIndex * m_metaInfo->getPieceLength();

  // fseek
  pthread_mutex_lock(fileLock);
  fseek(m_clientFile, piecePosStart, SEEK_SET);
  // write the buffer
  fwrite(piece->buf(), 1, piece->size(), m_clientFile);
  pthread_mutex_unlock(fileLock);

  // update bytes downloaded
  // TODO: critical section inside here
  m_metaInfo->increaseBytesDownloaded(pieceLength);

  return 0;
}

bool
Peer::allPiecesDone()
{
  for (int i=0; i<m_metaInfo->getNumPieces(); i++) 
  {
    if (!m_clientPiecesDone->at(i)) {
      return false;
    }
  }

  return true;
}

} // namespace sbt
