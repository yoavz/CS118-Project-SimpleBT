# 20-grading.sh

./kill.sh

rm -f ./text.txt
rm -f ./tools/text.txt
rm -f ./complete.tmp
rm -f ./test.result

cp ./tools/text.txt.bak ./tools/text.txt

./tools/sbt-tracker 60207 ./tools/test-2.torrent > test.result &

sleep 1

./tools/sbt-peer 11111 ./tools/test-2.torrent ./tools/ SIMPLEBT.TEST.111111 2>/dev/null &

# sleep 1

./build/simple-bt 60207 ./tools/test-2.torrent