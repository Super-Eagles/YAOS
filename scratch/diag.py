import time
import os
import subprocess
import collections

cores = os.cpu_count() or 1

def sample_times():
    cmd = 'wmic process get ProcessId, Name, UserModeTime, KernelModeTime, WorkingSetSize, ThreadCount, HandleCount /format:csv'
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, text=True, encoding='ansi')
    samples = {}
    for line in res.stdout.splitlines():
        line = line.strip()
        if not line or line.startswith('Node,') or 'ProcessId' in line:
            continue
        parts = line.split(',')
        if len(parts) >= 8:
            try:
                # CSV fields: Node, HandleCount, KernelModeTime, Name, ProcessId, ThreadCount, UserModeTime, WorkingSetSize
                handle_count = int(parts[1])
                kernel_time = int(parts[2])
                name = parts[3]
                pid = int(parts[4])
                thread_count = int(parts[5])
                user_time = int(parts[6])
                working_set = int(parts[7])
                samples[pid] = {
                    'name': name,
                    'total_time': kernel_time + user_time,
                    'threads': thread_count,
                    'handles': handle_count,
                    'mem_mb': round(working_set / (1024*1024), 2)
                }
            except (ValueError, IndexError):
                continue
    return samples

def main():
    print("Sampling CPU times (this takes 1.5 seconds)...")
    t1 = time.time()
    s1 = sample_times()
    time.sleep(1.5)
    t2 = time.time()
    s2 = sample_times()

    delta_time = t2 - t1
    total_possible_units = delta_time * 1e7 * cores

    cpu_stats = []
    for pid, data2 in s2.items():
        if pid in s1:
            delta_units = data2['total_time'] - s1[pid]['total_time']
            cpu_percent = (delta_units / total_possible_units) * 100.0 if total_possible_units > 0 else 0
            cpu_stats.append({
                'pid': pid,
                'name': data2['name'],
                'cpu': round(cpu_percent, 2),
                'mem_mb': data2['mem_mb'],
                'threads': data2['threads'],
                'handles': data2['handles']
            })

    print('\n=== Top 10 CPU Consuming Processes ===')
    top_cpu = sorted(cpu_stats, key=lambda x: x['cpu'], reverse=True)[:10]
    for p in top_cpu:
        print(f"PID: {p['pid']:<6} | Name: {p['name']:<25} | CPU: {p['cpu']:>5}% | Mem: {p['mem_mb']:>7} MB | Threads: {p['threads']:<3} | Handles: {p['handles']}")

    print('\n=== Top 10 Memory Consuming Processes ===')
    top_mem = sorted(cpu_stats, key=lambda x: x['mem_mb'], reverse=True)[:10]
    for p in top_mem:
        print(f"PID: {p['pid']:<6} | Name: {p['name']:<25} | CPU: {p['cpu']:>5}% | Mem: {p['mem_mb']:>7} MB | Threads: {p['threads']:<3} | Handles: {p['handles']}")

    print('\n=== IDE, WebView and YAOS Processes Details ===')
    found_any = False
    for p in sorted(cpu_stats, key=lambda x: x['cpu'], reverse=True):
        name_lower = p['name'].lower()
        if 'antigravity' in name_lower or 'webview' in name_lower or 'yaos' in name_lower or 'node' in name_lower or 'python' in name_lower:
            print(f"PID: {p['pid']:<6} | Name: {p['name']:<25} | CPU: {p['cpu']:>5}% | Mem: {p['mem_mb']:>7} MB | Threads: {p['threads']:<3} | Handles: {p['handles']}")
            found_any = True
    if not found_any:
        print("No active IDE, WebView or YAOS processes detected.")

if __name__ == '__main__':
    main()
