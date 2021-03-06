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

#ifndef SBT_CLIENT_HPP
#define SBT_CLIENT_HPP

#include <pthread.h>
#include "common.hpp"
#include "meta-info.hpp"
#include "tracker-response.hpp"
#include "peer.hpp"

namespace sbt {

class Client
{
public:
  class Error : public std::runtime_error
  {
  public:
    explicit
    Error(const std::string& what)
      : std::runtime_error(what)
    {
    }
  };

public:
  Client(const std::string& port,
         const std::string& torrent);

  void
  run();

  const std::string&
  getTrackerHost() {
    return m_trackerHost;
  }

  const std::string&
  getTrackerPort() {
    return m_trackerPort;
  }

  const std::string&
  getTrackerFile() {
    return m_trackerFile;
  }

private:
  void
  loadMetaInfo(const std::string& torrent);

  void
  connectTracker();

  void
  sendTrackerRequest();

  void
  recvTrackerResponse();

  void 
  prepareFile();

  static void
  log(std::string msg);

  static void 
  alarmHandler(int sig);

  static void
  closeFile(int sig);

  static void * 
  runHandshakePeer(void *peer);

  static void * 
  runAcceptPeer(void *peer);

  static void *
  acceptPeers(void *c);

  bool
  allPiecesDone();

  int
  addPeer(Peer *peer);

  bool
  peerRunning(uint16_t port);

private:
  MetaInfo m_metaInfo;
  std::string m_trackerHost;
  std::string m_trackerPort;
  std::string m_trackerFile;

  uint16_t m_clientPort;

  int m_trackerSock;
  int m_listeningSock;

  uint64_t m_interval;
  bool m_isFirstReq;
  bool m_isFirstRes;

  std::vector<bool> m_piecesDone;
  std::vector<bool> m_piecesLocked;

  // list of peers (from tracker)
  std::vector<Peer> m_peers;
  std::vector<uint16_t> m_portsRunning;

  pthread_t threads[20];
  static const int MAX_THREAD = 20;
  bool isUsed[20] = {0};

  pthread_mutex_t threadLock;
  pthread_mutex_t pieceLock;
  pthread_mutex_t fileLock;

  // for alarm functionality
  static bool m_alarm;
  static FILE *m_torrentFile;
};

} // namespace sbt

#endif // SBT_CLIENT_HPP
