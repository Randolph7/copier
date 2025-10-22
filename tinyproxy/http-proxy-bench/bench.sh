numactl --cpubind=0 --membind=0 ./src/tinyproxy -c etc/tinyproxy.conf -d

cat /proc/cpuinfo | grep MHz

numactl --cpubind=1 --membind=1 ./out/server

numactl --cpubind=1 --membind=1 ./out/client 1 512 200
numactl --cpubind=1 --membind=1 ./out/client 1 1024 200
numactl --cpubind=1 --membind=1 ./out/client 1 2048 200
numactl --cpubind=1 --membind=1 ./out/client 1 4096 200
numactl --cpubind=1 --membind=1 ./out/client 1 8192 200
numactl --cpubind=1 --membind=1 ./out/client 1 16384 200
numactl --cpubind=1 --membind=1 ./out/client 1 32768 200
numactl --cpubind=1 --membind=1 ./out/client 1 65536 200
numactl --cpubind=1 --membind=1 ./out/client 1 131072 200
numactl --cpubind=1 --membind=1 ./out/client 1 262144 200

sudo LD_PRELOAD=/home/hjk/copier/zIO/copy_interpose.so numactl --cpubind=0 --membind=0 ./src/tinyproxy -c etc/tinyproxy.conf -d
echo "4096 6291456 6291456" | sudo tee /proc/sys/net/ipv4/tcp_rmem


numactl --physcpubind=1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43,45,47,49,51,53,55 --membind=1 ./out/server

numactl --physcpubind=57,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,103,105,107,109,111 --membind=1 ./out/client 2 8192 200
numactl --physcpubind=57,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,103,105,107,109,111 --membind=1 ./out/client 4 8192 200
numactl --physcpubind=57,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,103,105,107,109,111 --membind=1 ./out/client 6 8192 200
numactl --physcpubind=57,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,103,105,107,109,111 --membind=1 ./out/client 8 8192 200
numactl --physcpubind=57,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,103,105,107,109,111 --membind=1 ./out/client 10 8192 200
numactl --physcpubind=57,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,103,105,107,109,111 --membind=1 ./out/client 12 8192 200
numactl --physcpubind=57,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,103,105,107,109,111 --membind=1 ./out/client 14 8192 200
numactl --physcpubind=57,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,103,105,107,109,111 --membind=1 ./out/client 16 8192 200
