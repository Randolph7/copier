# include this boilerplate
function jumpto
{
    label=$1
    cmd=$(sed -n "/$label:/{:a;n;p;ba};" $0 | grep -v ':$')
    eval "$cmd"
    exit
}

echo "Copier Copying TP. (0%) -- results/0-locality.txt"
sudo sysctl -w vm.nr_hugepages=8192

sudo cset shield --cpu=2 --kthread=on

touch results/0-locality.txt

cd bench
make
cd ..

dir=bench
lines=$(wc -l < results/0-locality.txt)

jumpto ${lines}

0:
echo "# ERMS AVX DMA Copier" >> results/0-locality.txt

1:
size=1024
echo "1K benchmark"
echo -n "1K " >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 0 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 3 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 1 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 4 | awk '{print $6}' >> results/0-locality.txt
sleep 0.5

2:
size=2048
echo "2K benchmark"
echo -n "2K " >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 0 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 3 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 1 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 4 | awk '{print $6}' >> results/0-locality.txt
sleep 0.5

3:
size=4096
echo "4K benchmark"
echo -n "4K " >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 0 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 3 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 1 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 128 4 | awk '{print $6}' >> results/0-locality.txt
sleep 0.5

4:
size=8192
echo "8K benchmark"
echo -n "8K " >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 64 0 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 64 3 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 64 1 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 64 4 | awk '{print $6}' >> results/0-locality.txt
sleep 0.5

5:
size=16384
echo "16K benchmark"
echo -n "16K " >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 0 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 3 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 1 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 6 | awk '{print $6}' >> results/0-locality.txt
sleep 0.5

6:
size=32768
echo "32K benchmark"
echo -n "32K " >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 0 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 3 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 1 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 6 | awk '{print $6}' >> results/0-locality.txt
sleep 0.5

7:
size=65536
echo "64K benchmark"
echo -n "64K " >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 0 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 3 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 1 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 6 | awk '{print $6}' >> results/0-locality.txt
sleep 0.5

8:
size=131072
echo "128K benchmark"
echo -n "128K " >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 0 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 3 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 1 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 6 | awk '{print $6}' >> results/0-locality.txt
sleep 0.5

9:
size=262144
echo "256K benchmark"
echo -n "256K " >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 0 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 3 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 1 | awk '{print $6}' ORS=' ' >> results/0-locality.txt
numactl --physcpubind=0 --membind=0 ./$dir/perf $size 32 6 | awk '{print $6}' >> results/0-locality.txt
sleep 0.5

10:
sudo sysctl -w vm.nr_hugepages=0
echo "finish Copier Copying TP. (0%) -- results/0-locality.txt"
