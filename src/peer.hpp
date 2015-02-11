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
   
  Peer (const std::string& peerId,
        const std::string& ip,
        uint16_t port,
        std::vector<bool>* clientPiecesDone,
        FILE *clientFile);

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

  const std::string&
  getPeerId()
  {
    return m_peerId;
  }

  void
  setPeerId(const std::string& peerId)
  {
    m_peerId = peerId;
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
  setClientData(std::vector<bool>* clientPiecesDone,
                MetaInfo *metaInfo,
                FILE *clientFile)
  {
    m_metaInfo = metaInfo;
    m_clientPiecesDone = clientPiecesDone;
    m_clientFile = clientFile;
  }

  int
  waitOnMessage();

  int
  waitOnHandshake();

private:
  std::string m_peerId;    
  std::string m_ip;
  uint16_t m_port;

  int m_sock;
  int m_activePiece;

  std::vector<bool> m_piecesDone;
  ConstBufferPtr m_bitfield;

  // client references
  MetaInfo *m_metaInfo;
  std::vector<bool>* m_clientPiecesDone;
  FILE *m_clientFile;

private:
  int connectSocket();

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
