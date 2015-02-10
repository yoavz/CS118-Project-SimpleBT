./tools/sbt-tracker 60207 ./tools/test-2.torrent > test.result & 

sleep 1

./tools/sbt-peer 11111 ./tools/test-2.torrent ./tools/ SIMPLEBT.TEST.111111 > peer.result & 

sleep 1

./build/simple-bt 60207 ./tools/test-2.torrent 
