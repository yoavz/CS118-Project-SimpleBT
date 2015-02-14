# 20-grading.sh

./kill.sh

rm -f ./text.txt
rm -f ./tools/text.txt
rm -f ./complete.tmp

cp ./tools/text.txt.bak ./tools/text.txt

echo "Run 2 test cases..."

./tools/sbt-tracker 60207 ./tools/test-2.torrent > /dev/null &

sleep 1

./tools/sbt-peer 11111 ./tools/test-2.torrent ./tools/ SIMPLEBT.TEST.111111 -e 2>/dev/null &
./tools/sbt-peer 22222 ./tools/test-2.torrent ./tools/ SIMPLEBT.TEST.222222 -e 2>/dev/null &

sleep 3

./build/simple-bt 60207 ./tools/test-2.torrent
