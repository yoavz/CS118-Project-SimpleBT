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
#include <signal.h>

namespace sbt {

bool Client::m_alarm = false;

Client::Client(const std::string& port, const std::string& torrent)
  : m_interval(3600)
  , m_isFirstReq(true)
  , m_isFirstRes(true)
{
  srand(time(NULL));

  m_clientPort = boost::lexical_cast<uint16_t>(port);

  loadMetaInfo(torrent);
  std::cout << "loaded metainfo" << std::endl;
  prepareFile();
  std::cout << "prepared file!" << std::endl;
  run();
}

void
Client::log(std::string msg)
{
  std::cout << "(Client): " << msg << std::endl;
}

void *
Client::acceptPeers(void *c)
{
  // since this is a static method, we 
  // must recover the class instance
  Client *client = static_cast<Client *>(c);

  client->log("Attempting to accept peers...");

  // create a TCP socket
  client->m_listeningSock = socket(AF_INET, SOCK_STREAM, 0);

  // allow others to reused address
  int yes =1;
  if (setsockopt(client->m_listeningSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    perror("setsockopt");
    return NULL;
  }

  // bind address to socket
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(40000);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
  memset(addr.sin_zero, '\0', sizeof(addr.sin_zero));
  if (bind(client->m_listeningSock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    perror("bind");
    return NULL;
  }

  // set the socket to listen
  if (listen(client->m_listeningSock, 10) == -1) {
    perror("listen");
    return NULL;
  }

  client->log("Listening on sock");

  while (true) {

    int threadId;
    bool foundThread = false;

    // check if we have enough threads and grab one 
    log(std::to_string(client->MAX_THREAD));
    pthread_mutex_lock(&client->thread_count_mutex);
    for (int i=0; i < client->MAX_THREAD; i++) {
      if (!client->isUsed[i]) {
        client->isUsed[i] = true;
        threadId = i;
        foundThread = true;
        client->log("grabbed thread " + std::to_string(threadId));
        break;
      }
    }
    pthread_mutex_unlock(&client->thread_count_mutex);


    // if no threads are found
    if (!foundThread) {
      sleep(1);
      continue;
    }

    client->log("accept()...");

    // wait for a connection with accept()
    struct sockaddr_in clientAddr;
    socklen_t clientAddrSize;
    int clientSockfd = accept(client->m_listeningSock, (struct sockaddr*)&clientAddr, &clientAddrSize);

    if (clientSockfd == -1) {
      perror("accept");
      return NULL;
    }

    char ipstr[INET_ADDRSTRLEN] = {'\0'};
    inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
    client->log("Accepted a connection from: " + std::string(ipstr) + ":" + std::to_string(ntohs(clientAddr.sin_port)));

    // initialize a peer and run it 
    Peer p(clientSockfd);
    pthread_create(&client->threads[threadId], 0, (Client::runAcceptPeer), static_cast<void*>(&p));

  }
  
}

// this function should be called from pthread_create
// it runs a peer who initiates the handshake
void * 
Client::runHandshakePeer(void *peer)
{
  Peer *p = static_cast<Peer *>(peer); 
  std::cout << "(Client): running handshake peer " << p->getPeerId() << std::endl;
  p->handshakeAndRun();

  return NULL;
}

// this function should be called from pthread_create
// it runs a peer who accepts the handshake
void * 
Client::runAcceptPeer(void *peer)
{
  Peer *p = static_cast<Peer *>(peer); 
  std::cout << "(Client): running accept peer " << p->getPeerId() << std::endl;
  p->respondAndRun();

  return NULL;
}

void
Client::alarmHandler(int sig)
{
  if (sig == SIGALRM) 
    m_alarm = true;

  return;
}


void
Client::run()
{
  connectTracker();
  sendTrackerRequest();
  recvTrackerResponse();

  // set the alarm to go off after m_interval seconds
  alarm(m_interval);

  // handle the alarm with this function
  signal(SIGALRM, alarmHandler);

  // setup listening
  // isUsed[1] = true;
  // pthread_create(&threads[1], 0, (Client::acceptPeers), static_cast<void*>(this));

  // attempt connecting to all peers 
  for (auto& peer : m_peers) {

      // iterator->first = key
      // iterator->second = value
      // Repeat if you also want to iterate through the second map.

      std::string peerPort = std::to_string(peer.getPort());
      if (peerPort == std::to_string(m_clientPort)) 
        continue;

      // pass references to the peers so that they can modify/access
      // piecesDone, the file, etc.
      peer.setClientData(&m_piecesDone, 
                         &m_piecesLocked, 
                         &m_metaInfo, 
                         &m_peers,
                         m_torrentFile);

      // run a peer in a new thread
      isUsed[0] = true;
      pthread_create(&threads[0], 0, (Client::runHandshakePeer), static_cast<void*>(&peer));

      //TODO: remove for multithreading
      //only connect to one peer
      break;
  }

  while (true) {

    // if alarm went off
    if (m_alarm) { 

      connectTracker();
      sendTrackerRequest();
      recvTrackerResponse();

      log("Sent/recieved tracker response. next interval: " + std::to_string(m_interval));

      // set the alarm to go off after m_interval seconds
      alarm(m_interval);

      // handle the alarm with this function
      signal(SIGALRM, alarmHandler);

      m_alarm = false;
    }

    sleep(0.5);
  }
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

  int upload = m_metaInfo.getBytesUploaded();
  int download = m_metaInfo.getBytesDownloaded();
  int left = m_metaInfo.getLength() - download;

  param.setInfoHash(m_metaInfo.getHash());
  param.setPeerId("SIMPLEBT-TEST-PEERID");
  param.setIp("127.0.0.1"); 
  param.setPort(m_clientPort); 
  param.setUploaded(upload);
  param.setDownloaded(download);
  param.setLeft(left); 
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

  // log("sent tracker request");
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
      m_peers.push_back(p);
    }

    m_isFirstRes = false;
  }

}

// Prepares the destination data file
// If it exists, this function scans it's bytes, compares it with the correct hashes
// and determines how much of the file needs to be redownloaded.
// If it doesn't exist, this function creates the file

void 
Client::prepareFile()
{
  std::string torrentFileName = m_metaInfo.getName();
  int fileLength = m_metaInfo.getLength();
  int pieceLength = m_metaInfo.getPieceLength();
  int pieceCount = m_metaInfo.getNumPieces(); 
  int finalPieceLength = fileLength % pieceLength;
  if (finalPieceLength == 0) finalPieceLength = pieceLength;

  // initialize all pieces to false
  m_piecesDone = std::vector<bool>(pieceCount);
  m_piecesLocked = std::vector<bool>(pieceCount);
  for (int i=0; i<pieceCount; i++) {
    m_piecesDone[i] = false;
    m_piecesLocked[i] = false;
  }

  // open the file for reading 
  m_torrentFile = (FILE*)malloc(sizeof(FILE));
  m_torrentFile = fopen (torrentFileName.c_str(), "r");

  // if file exists 
  if (m_torrentFile != NULL) {
    fseek(m_torrentFile, 0, SEEK_END);
    // and if it's the proper size
    if (ftell(m_torrentFile) == fileLength) {
      rewind(m_torrentFile);

      char *pBuf = (char *)malloc(sizeof(char) * pieceLength);
      uint64_t curPieceLength;

      for (int i=0; i<pieceCount; i++) {

        curPieceLength = i == pieceCount-1 ? finalPieceLength : pieceLength;
        if (fread(pBuf, 1, curPieceLength, m_torrentFile) != curPieceLength)
          log("fread error");

        OBufferStream os;
        os.write(pBuf, pieceLength);
        ConstBufferPtr pieceBuf = os.buf();

        if (equal(util::sha1(pieceBuf), m_metaInfo.getHashOfPiece(i))) {
          m_piecesDone[i] = true;
          m_piecesLocked[i] = true;
        }
        else {
          m_piecesDone[i] = false;
          m_piecesLocked[i] = false;
          // std::cout << "piece " << i << " bad" << std::endl;
        }
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


} // namespace sbt
