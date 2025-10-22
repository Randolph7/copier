# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

echo "Protobuf Copier"
sudo cset shield --cpu=2 --kthread=on

touch results/copier.txt

cd copier
make
cd ..

protobufDir=copier
lines=$(wc -l < results/copier.txt)


numactl --physcpubind=0 ./${protobufDir}/out/cpp-kv-server &
sleep 0.5

jumpto ${lines}

0:
size=1024
echo "1K benchmark"
echo -n "1K " >> results/copier.txt
numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size &> /dev/null
result=$(numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size | head -n 3 | tail -n 1 | awk '{print $3}' >> results/copier.txt)
sleep 0.5

1:
size=2048
echo "2K benchmark"
echo -n "2K " >> results/copier.txt
numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size &> /dev/null
result=$(numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size | head -n 3 | tail -n 1 | awk '{print $3}' >> results/copier.txt)
sleep 0.5

2:
size=4096
echo "4K benchmark"
echo -n "4K " >> results/copier.txt
numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size &> /dev/null
result=$(numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size | head -n 3 | tail -n 1 | awk '{print $3}' >> results/copier.txt)
sleep 0.5

3:
size=8192
echo "8K benchmark"
echo -n "8K " >> results/copier.txt
numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size &> /dev/null
result=$(numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size | head -n 3 | tail -n 1 | awk '{print $3}' >> results/copier.txt)
sleep 0.5

4:
size=16384
echo "16K benchmark"
echo -n "16K " >> results/copier.txt
numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size &> /dev/null
result=$(numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size | head -n 3 | tail -n 1 | awk '{print $3}' >> results/copier.txt)
sleep 0.5

5:
size=32768
echo "32K benchmark"
echo -n "32K " >> results/copier.txt
numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size &> /dev/null
result=$(numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size | head -n 3 | tail -n 1 | awk '{print $3}' >> results/copier.txt)
sleep 0.5

6:
size=65536
echo "64K benchmark"
echo -n "64K " >> results/copier.txt
numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size &> /dev/null
result=$(numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size | head -n 3 | tail -n 1 | awk '{print $3}' >> results/copier.txt)
sleep 0.5

7:
size=131072
echo "128K benchmark"
echo -n "128K " >> results/copier.txt
numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size &> /dev/null
result=$(numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size | head -n 3 | tail -n 1 | awk '{print $3}' >> results/copier.txt)
sleep 0.5

8:
size=262144
echo "256K benchmark"
echo -n "256K " >> results/copier.txt
numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size &> /dev/null
result=$(numactl --physcpubind=1 ./${protobufDir}/out/cpp-kv-client 20 200 $size | head -n 3 | tail -n 1 | awk '{print $3}' >> results/copier.txt)
sleep 0.5

9:
pkill cpp-kv-server
echo "finish Protobuf Copier"



# sudo cset shield --cpu=2 --kthread=on
