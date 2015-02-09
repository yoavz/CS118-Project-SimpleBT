/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef SBT_PEER_HPP
#define SBT_PEER_HPP

#include "common.hpp"
#include "meta-info.hpp"
#include "tracker-response.hpp"

namespace sbt {

class Peer 
{
public:
  Peer (const std::string& peerId,
        const std::string& ip,
        uint16_t port);

  void
  run();

public:

  int
  getSock()
  {
    return m_sock;
  }

  void
  setSock(int sock)
  {
    m_sock = sock;
  }

  bool
  getChoked()
  {
    return m_choked;
  }

  const std::string&
  getPeerId()
  {
    return m_peerId;
  }

  const std::string&
  getIp()
  {
    return m_ip;
  }

  uint16_t
  getPort()
  {
    return m_port;
  }

  void
  setPort(uint16_t port)
  {
    m_port = port;
  }

private:
  int m_sock;
  int m_activePiece;
  bool m_choked;
  bool m_interested;

  std::string m_peerId;    
  std::string m_ip;
  uint16_t m_port;

  std::vector<bool> m_piecesDone;
};

} // namespace sbt

#endif // SBT_PEER_HPP
