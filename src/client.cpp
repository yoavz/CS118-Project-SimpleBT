/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014,  Regents of the University of California
 *
 * This file is part of Simple BT.
 * See AUTHORS.md for complete list of Simple BT authors and contributors.
 *
 * NSL is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NSL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NSL, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 *
 * \author Yingdi Yu <yingdi@cs.ucla.edu>
 */

#include "client.hpp"
#include "tracker-request-param.hpp"
#include "tracker-response.hpp"
#include "http/http-request.hpp"
#include "http/http-response.hpp"
#include "util/hash.hpp"
#include "util/buffer-stream.hpp"
#include "msg/msg-base.hpp"
#include "msg/handshake.hpp"

#include <fstream>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>


namespace sbt {

Client::Client(const std::string& port, const std::string& torrent)
  : m_interval(3600)
  , m_isFirstReq(true)
  , m_isFirstRes(true)
{
  srand(time(NULL));

  m_clientPort = boost::lexical_cast<uint16_t>(port);

  loadMetaInfo(torrent);

  // prepareFile();
  // std::cout << "prepared file!" << std::endl;

  run();
}

void
Client::run()
{

  connectTracker();
  sendTrackerRequest();
  recvTrackerResponse();

  std::cout << "recieved and parsed tracker resp" << std::endl;

  for(std::map<std::string, Peer>::iterator it = m_peers.begin(); it != m_peers.end(); it++) {

      // iterator->first = key
      // iterator->second = value
      // Repeat if you also want to iterate through the second map.

      Peer *peer = &it->second;

      std::string peerPort = std::to_string(peer->getPort());
      if (peerPort == std::to_string(m_clientPort))
        continue;

      std::cout << "Connecting to " << peerPort << std::endl; 

      int peerSock = socket(AF_INET, SOCK_STREAM, 0);

      peer->setSock(peerSock);
      connectPeer(peer);
      std::cout << "Connected to " << peerPort << std::endl; 

      peerProcedure(peer);
  }

}

void
Client::connectPeer(Peer *peer) 
{
    struct addrinfo hints;
    struct addrinfo* res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    std::string peerPort = std::to_string(peer->getPort());

    // get address
    int status = 0;
    if ((status = getaddrinfo(peer->getIp().c_str(), peerPort.c_str(), &hints, &res)) != 0)
      throw Error("Cannot resolver peer ip");

    struct sockaddr_in* ipv4 = (struct sockaddr_in*)res->ai_addr;
    char ipstr[INET_ADDRSTRLEN] = {'\0'};
    inet_ntop(res->ai_family, &(ipv4->sin_addr), ipstr, sizeof(ipstr));
    // std::cout << "tracker address: " << ipstr << ":" << ntohs(ipv4->sin_port) << std::endl;

    if (connect(peer->getSock(), res->ai_addr, res->ai_addrlen) == -1) {
      perror("connect");
      throw Error("Cannot connect peer");
    }

    freeaddrinfo(res);
}

void
Client::loadMetaInfo(const std::string& torrent)
{
  std::ifstream is(torrent);
  m_metaInfo.wireDecode(is);

  std::string announce = m_metaInfo.getAnnounce();
  std::string url;
  std::string defaultPort;
  if (announce.substr(0, 5) == "https") {
    url = announce.substr(8);
    defaultPort = "443";
  }
  else if (announce.substr(0, 4) == "http") {
    url = announce.substr(7);
    defaultPort = "80";
  }
  else
    throw Error("Wrong tracker url, wrong scheme");

  size_t slashPos = url.find('/');
  if (slashPos == std::string::npos) {
    throw Error("Wrong tracker url, no file");
  }
  m_trackerFile = url.substr(slashPos);

  std::string host = url.substr(0, slashPos);

  size_t colonPos = host.find(':');
  if (colonPos == std::string::npos) {
    m_trackerHost = host;
    m_trackerPort = defaultPort;
  }
  else {
    m_trackerHost = host.substr(0, colonPos);
    m_trackerPort = host.substr(colonPos + 1);
  }
}

void
Client::connectTracker()
{
  m_trackerSock = socket(AF_INET, SOCK_STREAM, 0);

  struct addrinfo hints;
  struct addrinfo* res;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; // IPv4
  hints.ai_socktype = SOCK_STREAM;

  // get address
  int status = 0;
  if ((status = getaddrinfo(m_trackerHost.c_str(), m_trackerPort.c_str(), &hints, &res)) != 0)
    throw Error("Cannot resolver tracker ip");
  struct sockaddr_in* ipv4 = (struct sockaddr_in*)res->ai_addr;
  char ipstr[INET_ADDRSTRLEN] = {'\0'};
  inet_ntop(res->ai_family, &(ipv4->sin_addr), ipstr, sizeof(ipstr));
  // std::cout << "tracker address: " << ipstr << ":" << ntohs(ipv4->sin_port) << std::endl;

  if (connect(m_trackerSock, res->ai_addr, res->ai_addrlen) == -1) {
    perror("connect");
    throw Error("Cannot connect tracker");
  }

  freeaddrinfo(res);
}

void
Client::sendTrackerRequest()
{
  TrackerRequestParam param;

  param.setInfoHash(m_metaInfo.getHash());
  param.setPeerId("01234567890123456789"); //TODO:
  param.setIp("127.0.0.1"); //TODO:
  param.setPort(m_clientPort); //TODO:
  param.setUploaded(100); //TODO:
  param.setDownloaded(200); //TODO:
  param.setLeft(300); //TODO:
  if (m_isFirstReq)
    param.setEvent(TrackerRequestParam::STARTED);

  // std::string path = m_trackerFile;
  std::string path = m_metaInfo.getAnnounce();
  path += param.encode();

  HttpRequest request;
  request.setMethod(HttpRequest::GET);
  request.setHost(m_trackerHost);
  request.setPort(boost::lexical_cast<uint16_t>(m_trackerPort));
  request.setPath(path);
  request.setVersion("1.0");

  Buffer buffer(request.getTotalLength());

  request.formatRequest(reinterpret_cast<char *>(buffer.buf()));

  send(m_trackerSock, buffer.buf(), buffer.size(), 0);
}

void
Client::recvTrackerResponse()
{
  std::stringstream headerOs;
  std::stringstream bodyOs;

  char buf[512] = {0};
  char lastTree[3] = {0};

  bool hasEnd = false;
  bool hasParseHeader = false;
  HttpResponse response;

  uint64_t bodyLength = 0;

  while (true) {
    memset(buf, '\0', sizeof(buf));
    memcpy(buf, lastTree, 3);

    ssize_t res = recv(m_trackerSock, buf + 3, 512 - 3, 0);

    if (res == -1) {
      perror("recv");
      return;
    }

    const char* endline = 0;

    if (!hasEnd)
      endline = (const char*)memmem(buf, res, "\r\n\r\n", 4);

    if (endline != 0) {
      const char* headerEnd = endline + 4;

      headerOs.write(buf + 3, (endline + 4 - buf - 3));

      if (headerEnd < (buf + 3 + res)) {
        bodyOs.write(headerEnd, (buf + 3 + res - headerEnd));
      }

      hasEnd = true;
    }
    else {
      if (!hasEnd) {
        memcpy(lastTree, buf + res, 3);
        headerOs.write(buf + 3, res);
      }
      else
        bodyOs.write(buf + 3, res);
    }

    if (hasEnd) {
      if (!hasParseHeader) {
        response.parseResponse(headerOs.str().c_str(), headerOs.str().size());
        hasParseHeader = true;

        bodyLength = boost::lexical_cast<uint64_t>(response.findHeader("Content-Length"));
      }
    }

    if (hasParseHeader && bodyOs.str().size() >= bodyLength)
      break;
  }

  close(m_trackerSock);
  FD_CLR(m_trackerSock, &m_readSocks);

  bencoding::Dictionary dict;

  std::stringstream tss;
  tss.str(bodyOs.str());
  dict.wireDecode(tss);

  TrackerResponse trackerResponse;
  trackerResponse.decode(dict);
  std::vector<PeerInfo> infos = trackerResponse.getPeers();
  m_interval = trackerResponse.getInterval();

  if (m_isFirstRes) {
    for (const auto& peer : infos) {
      std::cout << peer.ip << ":" << peer.port << std::endl;
      Peer p(peer.peerId, peer.ip, peer.port);
      m_peers.insert( std::pair<std::string, Peer> (peer.peerId, p) );
    }
  }

  m_isFirstRes = false;
}

// TODO: fix allocation bug
void 
Client::prepareFile()
{
  
  std::string torrentFileName = m_metaInfo.getName();
  std::vector<uint8_t> pieces = m_metaInfo.getPieces();
  int64_t fileLength = m_metaInfo.getLength();
  int64_t pieceLength = m_metaInfo.getPieceLength();
  int64_t pieceCount = fileLength / pieceLength + (fileLength % pieceLength == 0 ? 0 : 1);
  int64_t finalPieceLength = fileLength % pieceLength;
  if (finalPieceLength == 0) finalPieceLength = pieceLength;

  std::cout << "Piece length: " << pieceLength << std::endl ;
  std::cout << "File length: " << fileLength << std::endl ;
  std::cout << "Pieces: " << pieces.size() << std::endl ;

  // m_piecesDone = std::vector<bool> (pieceCount);
  // initialize all pieces to false
  for (int64_t i=0; i<pieceCount; i++) {
    m_piecesDone.push_back(false);
  }

  m_torrentFile = (FILE*)malloc(sizeof(FILE));
  m_torrentFile = fopen (torrentFileName.c_str(), "r");

  // if file exists and it's a proper size
  if (m_torrentFile != NULL) {
    fseek(m_torrentFile, 0, SEEK_END);
    if (ftell(m_torrentFile) == fileLength) {

      fseek(m_torrentFile, 0, SEEK_SET);
      char *cBuf = new char[pieceLength];
      std::vector<uint8_t> sha1;

      for (int64_t i=0; i<pieceCount; i++) {
        fread(cBuf, 
              pieceLength, 
              i == pieceCount-1 ? finalPieceLength : pieceLength,
              m_torrentFile);

        Buffer pieceBuf(cBuf, pieceLength);
        sha1 = util::sha1(pieceBuf);
        bool pieceValid = true;

        for (int j=0; j<20; j++) {
          if (sha1.at(j) != pieces.at(i*20+j)) {
            pieceValid = false;
            break;
          }
        }

        m_piecesDone.at(i) = pieceValid;
      }

      return;
    } 
  }

  // file doesn't exist or it's not the right length
  // create and allocate to proper size 
  m_torrentFile = fopen(torrentFileName.c_str(), "w+");
  fseek(m_torrentFile, fileLength-1, SEEK_SET);
  fputc(0, m_torrentFile);

  return;
} 

//
void 
Client::peerProcedure(Peer *peer) 
{
  ConstBufferPtr resp = std::make_shared<Buffer> (1024, 1);
  int peerSock = peer->getSock();

  // Handshake that shi-
  msg::HandShake hs(m_metaInfo.getHash(), "SIMPLEBT-TEST-PEERID");
  ConstBufferPtr hsMsg = hs.encode();
  send(peerSock, hsMsg->buf(), hsMsg->size(), 0);

  std::cout << "Sent handshake" << std::endl;

  // wait for a handshake (length 68) 
  if ((resp = waitForResponse(peerSock, 68)) == NULL) {
    std::cout << "Resp error in peer " << std::endl;
    // pthread_exit(NULL);
    return;
  }

  msg::HandShake hsRsp;
  hsRsp.decode(resp);

  // TODO: error checking, retry here if msg is corrupted

  // check the info hashes match. if not, stop here
  if (memcmp(m_metaInfo.getHash()->buf(), 
             hsRsp.getInfoHash()->buf(), 
             20) != 0) {
    std::cout << "Detected incorrect info hash with peer " << std::endl;
    close(peerSock);
    // pthread_exit(NULL);
    return;
  }

  std::cout << "Recieved handshake, info hash match!" << std::endl;

  // Send bit field
  // TODO: if multithreading, this is a critical section
  
  // construct a bitfield
  int64_t fileLength = m_metaInfo.getLength();
  int64_t pieceLength = m_metaInfo.getPieceLength();
  int numPieces = fileLength / pieceLength + (fileLength % pieceLength == 0 ? 0 : 1);
  int numBytes = numPieces/8 + (numPieces%8 == 0 ? 0 : 1);

  // TODO: remove once you fix prepare file bug
  for (int i=0; i<numPieces; i++)
    m_piecesDone.push_back(false);

  char *bitfield = (char *) malloc(numBytes);
  memset(bitfield, 0, numBytes);

  int byteNum, bitNum;
  for (int count=0; count < numPieces; count++)
  {
    byteNum = count / 8;    
    bitNum = count % 8;

    if (m_piecesDone.at(count)) {
      // std::cout << "setting piece " << count << std::endl;
      *(bitfield+byteNum) |= 1 << (7-bitNum);
    } 
  }

  OBufferStream bfstream;
  bfstream.write(bitfield, numBytes);
  msg::Bitfield bf_struct(bfstream.buf());
  ConstBufferPtr bf = bf_struct.encode();

  std::cout << "constructed bitfield " << bf->size() << " " << numBytes << std::endl;
  send(peerSock, bf->buf(), bf->size(), 0);

  ConstBufferPtr bitfieldResp = std::make_shared<Buffer> (1024, 1);
  if ((bitfieldResp = waitForResponse(peerSock, bf->size())) == NULL) {
    std::cout << "Resp error in peer " << std::endl;
    // pthread_exit(NULL);
    return;
  }

  msg::Bitfield bf_resp;
  bf_resp.decode(bitfieldResp);
  peer->setBitfield(bf_resp.getBitfield(), numPieces);
  std::cout << "recieved/parsed bitfield" << std::endl;

  // TODO: more critical section 
  bool foundPiece = false;
  for (int i=0; i<numPieces; i++) {
    if (!m_piecesDone.at(i) && peer->hasPiece(i)) {
      peer->setActivePiece(i);
      foundPiece = true;
      std::cout << "Found needed piece " << i << std::endl;
      break;
    }
  }

  if (!foundPiece) {
    std::cout << "Did not find a piece with peer" << std::endl;
    // pthread_exit(NULL);
    return;
  }

  msg::Interested interested;
  ConstBufferPtr int_buf = interested.encode();

  std::cout << "sending interested" << std::endl;
  send(peerSock, int_buf->buf(), int_buf->size(), 0);

  ConstBufferPtr unchokeResp = std::make_shared<Buffer> (1024, 1);
  if ((unchokeResp = waitForResponse(peerSock, 1)) == NULL) {
    std::cout << "Resp error in peer " << std::endl;
    // pthread_exit(NULL);
    return;
  }

  // check that it's the right message
  // TODO: add try catch here?
  msg::Unchoke unchoke;
  unchoke.decode(unchokeResp);

  std::cout << "got unchoke" << std::endl;

  // TODO: handle half pieces
  msg::Request request(peer->getActivePiece(), 0, m_metaInfo.getPieceLength());
  ConstBufferPtr r_buf = request.encode();
  send(peerSock, r_buf->buf(), r_buf->size(), 0);

  std::cout << m_metaInfo.getPieceLength();

  ConstBufferPtr piece = std::make_shared<Buffer> (3000, 1);
  if ((piece = waitForResponse(peerSock, m_metaInfo.getPieceLength())) == NULL) 
  {
    std::cout << "Resp error in peer " << std::endl;
    // pthread_exit(NULL);
    return;
  }

  std::cout << "recieved the piece! length: " << piece->size() << std::endl;

  std::vector<uint8_t> recievedHash = *util::sha1(piece);
  std::vector<uint8_t> ourHash = m_metaInfo.getPieces(); 

  for (int i=0; i<20; i++) {
    if (recievedHash.at(i) != ourHash.at(i)) {
      std::cout << "difference at " << i << std::endl;
      std::cout << recievedHash.at(i) << " " << ourHash.at(i) << std::endl;
      return;
    }
  }

  std::cout << "same hash!" << std::endl;

  return;
}

ConstBufferPtr
Client::waitForResponse(int sockfd, int responseLen)
{

  OBufferStream obuf;
  int status;
  int total=0;
  char buf[512] = {0};

  while (total < responseLen) {
    memset(buf, '\0', sizeof(buf));
    if ((status = recv(sockfd, buf, 512, 0)) == -1) {
      perror("recv");
      return NULL;
    }

    total += status;

    if (status >= 512)
      obuf << buf;
    else
      obuf.write(buf, status);
  }  

  return obuf.buf();
}


// TODO:
// handles the response to the peer
void 
Client::handlePeerResponse(std::string peerId, const char *resp)
{
  return;
}

// TODO:
// handles the request sent
// int 
// Client::buildPeerResponse(std::string peerId, std::ofstream& resp) 
// {
//   return 0;
// }

} // namespace sbt
