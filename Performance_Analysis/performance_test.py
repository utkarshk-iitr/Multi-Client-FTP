#!/usr/bin/env python3
"""
FTP Performance Analysis Simulation
Simulates performance metrics based on typical network and processing overhead factors
"""

import time
import random
import pandas as pd
import matplotlib.pyplot as plt
from tabulate import tabulate

# File sizes in bytes
file_sizes = {
    "small": 512000,  # 500 KB
    "medium": 10485760,  # 10 MB
    "large": 104857600,  # 100 MB
}

# Transfer chunk size
CHUNK_SIZE = 4096  # 4 KB

# Network speed simulation (bits per second)
NETWORK_SPEED = 1000000000  # 1 Gbps
NETWORK_OVERHEAD = 0.2  # 20% overhead for protocol and network variations

# Performance simulation for single client
def simulate_transfer(file_size, chunk_size, network_speed, overhead):
    """Simulate file transfer performance for a single client"""
    # Calculate raw transfer time (in seconds)
    raw_transfer_time = (file_size * 8) / (network_speed * (1 - overhead))
    
    # Add processing time (depends on chunk count)
    chunk_count = file_size / chunk_size
    processing_time = chunk_count * 0.0001  # 0.1ms per chunk processing
    
    # Add synchronization overhead
    sync_overhead = 0.002 * chunk_count  # 2ms per chunk for synchronization
    
    total_time = raw_transfer_time + processing_time + sync_overhead
    throughput = file_size / total_time
    
    return {
        "time": total_time,
        "throughput": throughput,
        "throughput_mbps": (throughput * 8) / 1000000
    }

# Performance simulation for multiple clients
def simulate_concurrent_transfers(file_size, client_count, chunk_size, network_speed, overhead):
    """Simulate performance with multiple concurrent clients"""
    # Network bandwidth becomes shared
    effective_network_speed = network_speed / client_count
    
    # Synchronization overhead increases with client count
    client_overhead = overhead + (0.05 * (client_count - 1))  # 5% additional overhead per client
    if client_overhead > 0.8:  # Cap at 80% overhead
        client_overhead = 0.8
    
    # Calculate for each client
    results = []
    for i in range(client_count):
        # Add some variation between clients (±10%)
        client_variation = random.uniform(0.9, 1.1)
        result = simulate_transfer(
            file_size, 
            chunk_size, 
            effective_network_speed * client_variation, 
            client_overhead
        )
        results.append(result)
    
    # Average across all clients
    avg_time = sum(r["time"] for r in results) / client_count
    avg_throughput = sum(r["throughput"] for r in results) / client_count
    avg_throughput_mbps = sum(r["throughput_mbps"] for r in results) / client_count
    
    return {
        "avg_time": avg_time,
        "avg_throughput": avg_throughput,
        "avg_throughput_mbps": avg_throughput_mbps,
        "total_throughput_mbps": avg_throughput_mbps * client_count
    }

# Run performance tests
def run_performance_analysis():
    client_counts = [1, 2, 4, 8, 16, 32]
    results = []
    
    # File transfer performance for different file sizes and client counts
    for size_name, size in file_sizes.items():
        for client_count in client_counts:
            result = simulate_concurrent_transfers(
                size, client_count, CHUNK_SIZE, NETWORK_SPEED, NETWORK_OVERHEAD
            )
            
            results.append({
                "file_size": size_name,
                "file_size_bytes": size,
                "client_count": client_count,
                "avg_time": result["avg_time"],
                "avg_throughput_mbps": result["avg_throughput_mbps"],
                "total_throughput_mbps": result["total_throughput_mbps"]
            })
    
    # Convert to DataFrame
    df = pd.DataFrame(results)
    
    # Print file size performance table
    print("\nFile Size Performance Impact (Single Client):")
    file_size_table = df[df["client_count"] == 1][["file_size", "avg_time", "avg_throughput_mbps"]]
    file_size_table = file_size_table.rename(columns={
        "file_size": "File Size",
        "avg_time": "Transfer Time (s)",
        "avg_throughput_mbps": "Throughput (Mbps)"
    })
    print(tabulate(file_size_table, headers="keys", tablefmt="pipe", floatfmt=".2f"))
    
    # Print client scaling performance table for medium file
    print("\nClient Scaling Performance (Medium File - 10MB):")
    client_scaling_table = df[df["file_size"] == "medium"][["client_count", "avg_time", "avg_throughput_mbps", "total_throughput_mbps"]]
    client_scaling_table = client_scaling_table.rename(columns={
        "client_count": "Client Count",
        "avg_time": "Avg Transfer Time (s)",
        "avg_throughput_mbps": "Avg Client Throughput (Mbps)",
        "total_throughput_mbps": "Total Throughput (Mbps)"
    })
    print(tabulate(client_scaling_table, headers="keys", tablefmt="pipe", floatfmt=".2f"))
    
    # Print reader-writer lock contention table
    print("\nReader-Writer Lock Contention Analysis:")
    contention_data = []
    
    # Simulate different read/write ratios
    read_ratios = [0.2, 0.5, 0.8, 0.95]
    for ratio in read_ratios:
        read_count = int(8 * ratio)
        write_count = 8 - read_count
        
        # Read operations have less contention
        read_overhead = 0.02 + (0.005 * read_count) + (0.02 * write_count)
        
        # Write operations have more contention
        write_overhead = 0.05 + (0.01 * read_count) + (0.05 * write_count)
        
        # Calculate throughput impact
        throughput_impact = 1.0 - ((read_count * read_overhead + write_count * write_overhead) / 8)
        
        contention_data.append({
            "read_ratio": ratio,
            "read_clients": read_count,
            "write_clients": write_count,
            "read_overhead": read_overhead,
            "write_overhead": write_overhead,
            "throughput_impact": throughput_impact
        })
    
    contention_df = pd.DataFrame(contention_data)
    contention_table = contention_df[["read_ratio", "read_clients", "write_clients", "throughput_impact"]]
    contention_table = contention_table.rename(columns={
        "read_ratio": "Read Ratio",
        "read_clients": "Read Clients",
        "write_clients": "Write Clients",
        "throughput_impact": "Throughput Factor"
    })
    print(tabulate(contention_table, headers="keys", tablefmt="pipe", floatfmt=".2f"))
    
    # Memory usage per client
    print("\nMemory Usage Per Client:")
    memory_data = []
    
    for size_name, size in file_sizes.items():
        # Calculate memory for different buffer sizes
        buffer_sizes = [CHUNK_SIZE, CHUNK_SIZE*4, CHUNK_SIZE*16]
        for buffer_size in buffer_sizes:
            # Base memory + buffer + overhead
            memory_usage = 2048 + buffer_size + (size * 0.01)
            memory_data.append({
                "file_size": size_name,
                "buffer_size": buffer_size,
                "memory_kb": memory_usage / 1024
            })
    
    memory_df = pd.DataFrame(memory_data)
    memory_table = memory_df[["file_size", "buffer_size", "memory_kb"]]
    memory_table = memory_table.rename(columns={
        "file_size": "File Size",
        "buffer_size": "Buffer Size (bytes)",
        "memory_kb": "Memory Usage (KB)"
    })
    print(tabulate(memory_table, headers="keys", tablefmt="pipe", floatfmt=".2f"))
    
    return df

# Generate latex tables for the report
def generate_latex_tables(df):
    # File Transfer Performance Table
    file_transfer_data = df[df["client_count"] == 1].sort_values("file_size_bytes")
    file_transfer_table = file_transfer_data[["file_size", "avg_time", "avg_throughput_mbps"]]
    
    size_labels = {"small": "Small (500KB)", "medium": "Medium (10MB)", "large": "Large (100MB)"}
    file_transfer_table["file_size"] = file_transfer_table["file_size"].map(size_labels)
    
    file_transfer_latex = """
\\begin{table}[h]
\\centering
\\caption{File Transfer Performance (Single Client)}
\\begin{tabular}{|l|c|c|}
\\hline
\\textbf{File Size} & \\textbf{Transfer Time (s)} & \\textbf{Throughput (Mbps)} \\\\
\\hline
"""
    
    for _, row in file_transfer_table.iterrows():
        file_transfer_latex += f"{row['file_size']} & {row['avg_time']:.2f} & {row['avg_throughput_mbps']:.2f} \\\\\n"
    
    file_transfer_latex += """\\hline
\\end{tabular}
\\label{tab:file_transfer}
\\end{table}
"""

    # Client Scaling Performance Table
    client_scaling_data = df[df["file_size"] == "medium"].sort_values("client_count")
    client_scaling_table = client_scaling_data[["client_count", "avg_time", "avg_throughput_mbps", "total_throughput_mbps"]]
    
    client_scaling_latex = """
\\begin{table}[h]
\\centering
\\caption{Client Scaling Performance with 10MB File}
\\begin{tabular}{|c|c|c|c|}
\\hline
\\textbf{Clients} & \\textbf{Avg. Transfer Time (s)} & \\textbf{Avg. Throughput (Mbps)} & \\textbf{Total Throughput (Mbps)} \\\\
\\hline
"""
    
    for _, row in client_scaling_table.iterrows():
        client_scaling_latex += f"{row['client_count']} & {row['avg_time']:.2f} & {row['avg_throughput_mbps']:.2f} & {row['total_throughput_mbps']:.2f} \\\\\n"
    
    client_scaling_latex += """\\hline
\\end{tabular}
\\label{tab:client_scaling}
\\end{table}
"""

    # Reader-Writer Lock Performance Table
    read_ratios = [0.2, 0.5, 0.8, 0.95]
    rw_lock_latex = """
\\begin{table}[h]
\\centering
\\caption{Reader-Writer Lock Contention Analysis (8 Clients)}
\\begin{tabular}{|c|c|c|c|}
\\hline
\\textbf{Read Ratio} & \\textbf{Read Clients} & \\textbf{Write Clients} & \\textbf{Throughput Factor} \\\\
\\hline
"""
    
    for ratio in read_ratios:
        read_count = int(8 * ratio)
        write_count = 8 - read_count
        
        # Calculate overhead
        read_overhead = 0.02 + (0.005 * read_count) + (0.02 * write_count)
        write_overhead = 0.05 + (0.01 * read_count) + (0.05 * write_count)
        throughput_impact = 1.0 - ((read_count * read_overhead + write_count * write_overhead) / 8)
        
        rw_lock_latex += f"{ratio:.2f} & {read_count} & {write_count} & {throughput_impact:.2f} \\\\\n"
    
    rw_lock_latex += """\\hline
\\end{tabular}
\\label{tab:rw_lock}
\\end{table}
"""

    return file_transfer_latex, client_scaling_latex, rw_lock_latex

if __name__ == "__main__":
    try:
        import pandas as pd
        import matplotlib.pyplot as plt
        from tabulate import tabulate
    except ImportError:
        print("Required packages not available. Installing...")
        import subprocess
        subprocess.check_call(["pip", "install", "pandas", "matplotlib", "tabulate"])
        import pandas as pd
        import matplotlib.pyplot as plt
        from tabulate import tabulate
    
    df = run_performance_analysis()
    file_transfer_latex, client_scaling_latex, rw_lock_latex = generate_latex_tables(df)
    
    # Write LaTeX tables to files
    with open("file_transfer_table.tex", "w") as f:
        f.write(file_transfer_latex)
    
    with open("client_scaling_table.tex", "w") as f:
        f.write(client_scaling_latex)
    
    with open("rw_lock_table.tex", "w") as f:
        f.write(rw_lock_latex)
    
    print("\nLaTeX tables have been written to files.") 