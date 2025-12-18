#!/usr/bin/env python3
"""
Redis GET Copier 测试结果可视化脚本
生成三张图：吞吐量、能耗、吞吐/能耗
"""

import os
import re
import glob
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict

# 设置字体（使用英文标签避免字体问题）
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial', 'Liberation Sans']
plt.rcParams['axes.unicode_minus'] = False

def parse_log_file(filepath):
    """解析日志文件，提取关键数据"""
    data = {}
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # 从文件名提取数据大小和频率
    filename = os.path.basename(filepath)
    # 格式: get-copier_16K_freq1.4g_20251218_010115.log
    size_match = re.search(r'get-copier_(\d+)K', filename)
    freq_match = re.search(r'freq([\d.]+)g', filename)
    
    if size_match:
        data['size_k'] = int(size_match.group(1))
    if freq_match:
        data['freq_ghz'] = float(freq_match.group(1))
    
    # 从内容提取吞吐量（GB/s）
    throughput_match = re.search(r'吞吐量:\s*([\d.]+)\s*GB/s', content)
    if throughput_match:
        data['throughput_gbps'] = float(throughput_match.group(1))
    
    # 从内容提取总能耗（J）
    energy_match = re.search(r'总能耗:\s*[\d]+\s*uJ\s*\(([\d.]+)\s*J\)', content)
    if energy_match:
        data['energy_j'] = float(energy_match.group(1))
    
    # 从内容提取数据大小（备用，如果文件名解析失败）
    if 'size_k' not in data:
        size_match = re.search(r'数据大小:\s*(\d+)\s*字节\s*\((\d+)K\)', content)
        if size_match:
            data['size_k'] = int(size_match.group(2))
    
    # 从内容提取频率（备用，如果文件名解析失败）
    if 'freq_ghz' not in data:
        freq_match = re.search(r'频率:\s*([\d.]+)\s*GHz', content)
        if freq_match:
            data['freq_ghz'] = float(freq_match.group(1))
    
    return data

def load_all_data(result_dir):
    """加载所有日志文件的数据"""
    log_files = glob.glob(os.path.join(result_dir, 'get-copier_*.log'))
    
    all_data = []
    for log_file in log_files:
        data = parse_log_file(log_file)
        if all(key in data for key in ['size_k', 'freq_ghz', 'throughput_gbps', 'energy_j']):
            all_data.append(data)
        else:
            print(f"Warning: Unable to parse file {log_file}, missing data: {data}")
    
    return all_data

def organize_data(all_data):
    """按数据大小组织数据"""
    organized = defaultdict(lambda: {'freqs': [], 'throughputs': [], 'energies': [], 'efficiency': []})
    
    for data in all_data:
        size_k = data['size_k']
        organized[size_k]['freqs'].append(data['freq_ghz'])
        organized[size_k]['throughputs'].append(data['throughput_gbps'])
        organized[size_k]['energies'].append(data['energy_j'])
        if data['energy_j'] > 0:
            organized[size_k]['efficiency'].append(data['throughput_gbps'] / data['energy_j'])
        else:
            organized[size_k]['efficiency'].append(0)
    
    # 对每个size的数据按频率排序
    for size_k in organized:
        zipped = list(zip(organized[size_k]['freqs'], 
                         organized[size_k]['throughputs'],
                         organized[size_k]['energies'],
                         organized[size_k]['efficiency']))
        zipped.sort(key=lambda x: x[0])
        organized[size_k]['freqs'] = [x[0] for x in zipped]
        organized[size_k]['throughputs'] = [x[1] for x in zipped]
        organized[size_k]['energies'] = [x[2] for x in zipped]
        organized[size_k]['efficiency'] = [x[3] for x in zipped]
    
    return organized

def plot_throughput(organized_data, output_dir):
    """绘制吞吐量图"""
    plt.figure(figsize=(10, 6))
    
    # 为不同size选择不同颜色和标记
    colors = plt.cm.tab10(np.linspace(0, 1, len(organized_data)))
    markers = ['o', 's', '^', 'D', 'v', 'p', '*', 'h']
    
    for idx, (size_k, data) in enumerate(sorted(organized_data.items())):
        label = f'{size_k}K'
        color = colors[idx % len(colors)]
        marker = markers[idx % len(markers)]
        
        plt.plot(data['freqs'], data['throughputs'], 
                marker=marker, label=label, linewidth=2, 
                markersize=8, color=color)
    
    plt.xlabel('CPU Frequency (GHz)', fontsize=12)
    plt.ylabel('Throughput (GB/s)', fontsize=12)
    plt.title('Redis GET Copier Throughput vs CPU Frequency', fontsize=14, fontweight='bold')
    plt.legend(title='Data Size', fontsize=10, title_fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'throughput.png')
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Throughput plot saved: {output_path}")
    plt.close()

def plot_energy(organized_data, output_dir):
    """绘制能耗图"""
    plt.figure(figsize=(10, 6))
    
    colors = plt.cm.tab10(np.linspace(0, 1, len(organized_data)))
    markers = ['o', 's', '^', 'D', 'v', 'p', '*', 'h']
    
    for idx, (size_k, data) in enumerate(sorted(organized_data.items())):
        label = f'{size_k}K'
        color = colors[idx % len(colors)]
        marker = markers[idx % len(markers)]
        
        plt.plot(data['freqs'], data['energies'], 
                marker=marker, label=label, linewidth=2, 
                markersize=8, color=color)
    
    plt.xlabel('CPU Frequency (GHz)', fontsize=12)
    plt.ylabel('Energy Consumption (J)', fontsize=12)
    plt.title('Redis GET Copier Energy Consumption vs CPU Frequency', fontsize=14, fontweight='bold')
    plt.legend(title='Data Size', fontsize=10, title_fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'energy.png')
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Energy plot saved: {output_path}")
    plt.close()

def plot_efficiency(organized_data, output_dir):
    """绘制吞吐/能耗图（能效比）"""
    plt.figure(figsize=(10, 6))
    
    colors = plt.cm.tab10(np.linspace(0, 1, len(organized_data)))
    markers = ['o', 's', '^', 'D', 'v', 'p', '*', 'h']
    
    for idx, (size_k, data) in enumerate(sorted(organized_data.items())):
        label = f'{size_k}K'
        color = colors[idx % len(colors)]
        marker = markers[idx % len(markers)]
        
        plt.plot(data['freqs'], data['efficiency'], 
                marker=marker, label=label, linewidth=2, 
                markersize=8, color=color)
    
    plt.xlabel('CPU Frequency (GHz)', fontsize=12)
    plt.ylabel('Energy Efficiency (GB/s / J)', fontsize=12)
    plt.title('Redis GET Copier Energy Efficiency vs CPU Frequency', fontsize=14, fontweight='bold')
    plt.legend(title='Data Size', fontsize=10, title_fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'efficiency.png')
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Efficiency plot saved: {output_path}")
    plt.close()

def main():
    # 结果目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    result_dir = os.path.join(script_dir, 'results', 'get-copier-power')
    output_dir = os.path.join(script_dir, 'results', 'get-copier-power')
    
    if not os.path.exists(result_dir):
        print(f"Error: Result directory does not exist: {result_dir}")
        return
    
    print(f"Loading data from {result_dir}...")
    all_data = load_all_data(result_dir)
    
    if not all_data:
        print("Error: No valid data found")
        return
    
    print(f"Successfully loaded {len(all_data)} data records")
    
    # 组织数据
    organized_data = organize_data(all_data)
    print(f"Data organized by {len(organized_data)} data sizes")
    
    # 生成图表
    print("\nGenerating visualization plots...")
    plot_throughput(organized_data, output_dir)
    plot_energy(organized_data, output_dir)
    plot_efficiency(organized_data, output_dir)
    
    print("\nAll plots generated successfully!")

if __name__ == '__main__':
    main()

