#!/bin/bash

# Redis GET Copier 吞吐量和能耗测试脚本
# 测试不同 CPU 频率下的性能和能耗

echo "Redis GET Copier 吞吐量和能耗测试 -- results/get-copier-power/"

# 创建结果目录
result_dir=./results/get-copier-power
mkdir -p "$result_dir"

# CPU 频率配置（单位：GHz）
FREQ_LIST=(1.2 1.4 1.6 1.8 2.0 2.4 2.8 3.0 3.3)

# RAPL energy_uj 文件路径
RAPL_ENERGY_FILE="/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj"

# 总数据量：1GB
TOTAL_DATA_SIZE=$((1024 * 1024 * 1024))  # 1GB in bytes

# 数据大小配置（字节）
declare -a size_arr=(1024 4096 16384 65536 131072 262144)

# Copier CPU（根据 get-copier.sh，使用 CPU 3）
copier_cpu=2

# 设置 CPU 频率的函数（单位：GHz）
set_cpu_freq() {
    local cpu=$1
    local freq_ghz=$2
    local freq_str="${freq_ghz}GHz"
    
    sudo cpufreq-set -c "$cpu" -d "$freq_str" -u "4GHz" -g userspace 2>/dev/null
    sudo cpufreq-set -c "$cpu" -f "1GHz" 2>/dev/null
    sleep 0.2
    
    # 验证频率是否设置成功
    current_freq=$(cat /sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_cur_freq 2>/dev/null)
    if [ -n "$current_freq" ]; then
        current_freq_ghz=$(echo "scale=2; $current_freq / 1000000" | bc)
        echo "CPU ${cpu} 频率设置为: ${freq_ghz} GHz (当前: ${current_freq_ghz} GHz)"
    else
        echo "警告: 无法验证 CPU ${cpu} 的频率"
    fi
}

# 读取 RAPL energy_uj 值
read_energy_uj() {
    if [ -f "$RAPL_ENERGY_FILE" ]; then
        sudo cat "$RAPL_ENERGY_FILE" 2>/dev/null || echo "0"
    else
        echo "0"
    fi
}

# 设置 cset shield
echo "设置 CPU shield..."
sudo cset shield --cpu=${copier_cpu} --kthread=on

# 编译 Redis
echo "编译 Redis..."
cd redis-6.2-copier-get
make MALLOC=libc
cd ..

redisDir=redis-6.2-copier-get

# 运行所有测试组合
for freq_ghz in "${FREQ_LIST[@]}"; do
    echo "=========================================="
    echo "设置 Copier CPU (${copier_cpu}) 频率为 ${freq_ghz} GHz"
    echo "=========================================="
    
    # 设置 copier CPU 的频率
    set_cpu_freq ${copier_cpu} ${freq_ghz}
    
    for size in "${size_arr[@]}"; do
        size_k=$(($size / 1024))
        
        echo "----------------------------------------"
        echo "测试数据大小: ${size} 字节 (${size_k}K), 频率: ${freq_ghz} GHz"
        echo "----------------------------------------"
        
        # 创建结果文件名
        freq_tag=$(echo "$freq_ghz" | tr '.' '.')
        timestamp=$(date +"%Y%m%d_%H%M%S")
        result_file="${result_dir}/get-copier_${size_k}K_freq${freq_tag}g_${timestamp}.log"
        
        # 启动 Redis 服务器
        echo "启动 Redis 服务器..."
        numactl --physcpubind=0 ./${redisDir}/src/redis-server ${redisDir}/config/non-persistent.conf > /dev/null 2>&1 &
        redis_pid=$!
        sleep 0.5
        
        # 检查 Redis 是否成功启动
        if ! kill -0 $redis_pid 2>/dev/null; then
            echo "错误: Redis 服务器启动失败"
            continue
        fi

        # 计算需要的请求数量以达到1GB总数据量
        num_requests=$((TOTAL_DATA_SIZE / size))
        
        # 预热：设置一个键
        echo "预热..."
        numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t set -d $size -c 1 -n 1 -p 6379 > /dev/null 2>&1
        
        # 记录测试开始时间
        start_time=$(date +%s.%N)
        
        # 写入测试信息到结果文件
        {
            echo "=========================================="
            echo "Redis GET Copier 吞吐量和能耗测试"
            echo "=========================================="
            echo "测试开始时间: $(date)"
            echo "数据大小: ${size} 字节 (${size_k}K)"
            echo "总数据量: 1GB (${TOTAL_DATA_SIZE} 字节)"
            echo "请求数量: ${num_requests}"
            echo "Copier CPU: ${copier_cpu}"
            echo "频率: ${freq_ghz} GHz"
            echo "----------------------------------------"
        } | tee "$result_file"
        
        # 读取测试前的能耗值
        start_energy_uj=$(read_energy_uj)
        
        echo "运行 Redis GET benchmark 并测量能耗..."
        echo "开始能耗: ${start_energy_uj} uJ" | tee -a "$result_file"
        
        # 运行 benchmark 并捕获吞吐量
        benchmark_output=$(numactl --physcpubind=1 ./${redisDir}/src/redis-benchmark -t get -d $size -c 8 -n $num_requests -p 6379 2>&1)
        
        # 从输出中提取吞吐量
        # Redis benchmark 输出格式可能是：
        # "throughput summary: 12345.67 requests per second"
        # 或 "GET: 12345.67 requests per second, p50=0.123 msec"
        throughput_rps=$(echo "$benchmark_output" | grep -iE "(requests per second|throughput summary)" | \
            grep -oE '[0-9]+\.[0-9]+|[0-9]+' | head -n 1)
        
        # 将吞吐量从 requests/sec 转换为 GB/s
        # GB/s = (requests/sec) * (bytes per request) / (1024^3)
        if [ -n "$throughput_rps" ] && echo "$throughput_rps" | grep -qE '^[0-9]+\.?[0-9]*$'; then
            throughput_gbps=$(echo "scale=3; $throughput_rps * $size / 1024 / 1024 / 1024" | bc -l)
        else
            throughput_gbps="0"
            if [ -z "$throughput_rps" ]; then
                throughput_rps="N/A"
            fi
        fi
        
        # 读取测试后的能耗值
        end_energy_uj=$(read_energy_uj)
        
        end_time=$(date +%s.%N)
        duration=$(echo "$end_time - $start_time" | bc -l)
        
        # 计算能耗差值（处理溢出情况）
        # 使用 bc 来处理大数运算
        if [ "$end_energy_uj" -gt "$start_energy_uj" ] 2>/dev/null; then
            energy_uj=$((end_energy_uj - start_energy_uj))
        else
            # 处理计数器溢出（假设是64位计数器，最大值 2^64 - 1）
            # 使用 bc 计算：end + (2^64 - start)
            max_uj=$(echo "2^64 - 1" | bc | tr -d '\n')
            energy_uj=$(echo "$end_energy_uj + ($max_uj - $start_energy_uj + 1)" | bc | tr -d '\n')
        fi
        
        # 转换为焦耳
        energy_j=$(echo "scale=6; $energy_uj / 1000000" | bc -l)
        
        # 停止 Redis 服务器
        pkill redis-server
        sleep 0.5

        # 计算实际传输的数据量（可能略小于1GB，因为请求数量是整数除法）
        actual_data_size=$((num_requests * size))
        actual_data_gb=$(echo "scale=3; $actual_data_size / 1024 / 1024 / 1024" | bc -l)
        
        # 记录结果
        {
            echo "----------------------------------------"
            echo "测试结果:"
            echo "吞吐量: ${throughput_gbps} GB/s"
            if [ -n "$throughput_rps" ] && [ "$throughput_rps" != "0" ]; then
                echo "吞吐量 (requests/sec): ${throughput_rps}"
            fi
            echo "实际传输数据量: ${actual_data_size} 字节 (${actual_data_gb} GB)"
            echo "请求数量: ${num_requests}"
            echo "测试持续时间: ${duration} 秒"
            echo ""
            echo "能耗测量结果:"
            echo "开始能耗: ${start_energy_uj} uJ"
            echo "结束能耗: ${end_energy_uj} uJ"
            echo "总能耗: ${energy_uj} uJ (${energy_j} J)"
        } | tee -a "$result_file"
        
        {
            echo "----------------------------------------"
            echo "测试完成时间: $(date)"
            echo "结果已保存到: $result_file"
            echo ""
        } | tee -a "$result_file"
        
        echo "结果已保存: $result_file"
        echo ""
        
        # 等待一段时间再进行下一个测试
        sleep 1
    done
done

# 恢复 CPU 频率调节器（可选）
echo "恢复 CPU 频率调节器..."
for cpu in ${copier_cpu}; do
    if [ -d /sys/devices/system/cpu/cpu${cpu}/cpufreq ]; then
        echo ondemand > /sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor 2>/dev/null || \
        echo powersave > /sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor 2>/dev/null || \
        echo performance > /sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor 2>/dev/null
    fi
done

echo "=========================================="
echo "所有测试完成！结果保存在: $result_dir"
echo "=========================================="
