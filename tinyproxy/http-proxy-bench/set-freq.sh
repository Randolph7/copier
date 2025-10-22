for i in {0..47}
do
   sudo cpufreq-set -c $i -f 2200MHz
done

# for i in {0..111}
# do
#    sudo cpupower -c $i frequency-set -f 2200MHz
# done
