/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef SBT_PEER_HPP
#define SBT_PEER_HPP

#include "common.hpp"
#include "meta-info.hpp"
#include "tracker-response.hpp"
// #include "client.hpp"

namespace sbt {

class Client;

class Peer 
{
public:
  Peer (const std::string& peerId,
        const std::string& ip,
        uint16_t port);

  void
  handshakeAndRun();

  void
  setBitfield(ConstBufferPtr bitfield, int size);

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

  // bool
  // getChoked()
  // {
  //   return m_choked;
  // }

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

  bool
  hasPiece(int pieceNum)
  {
    return m_piecesDone.at(pieceNum);
  }

  int
  getActivePiece()
  {
    return m_activePiece;
  }

  void
  setActivePiece(int pieceNum)
  {
    m_activePiece = pieceNum;
  }

  void
  setClient(Client *client)
  {
    m_client = client;
  }

  Client 
  *getClient()
  {
    return m_client;
  }

  int
  waitOnMessage();

private:
  Client* m_client;

  std::string m_peerId;    
  std::string m_ip;
  uint16_t m_port;

  int m_sock;
  int m_activePiece;

  // bool m_choked;
  // bool m_interested;

  std::vector<bool> m_piecesDone;
  ConstBufferPtr m_bitfield;

private:
  void log(std::string msg);

  void handleUnchoke(ConstBufferPtr cbf);
  void handleInterested(ConstBufferPtr cbf);
  void handleHave(ConstBufferPtr cbf);
  void handleBitfield(ConstBufferPtr cbf);
  void handleRequest(ConstBufferPtr cbf);
  void handlePiece(ConstBufferPtr cbf);

};

} // namespace sbt

#endif // SBT_PEER_HPP
