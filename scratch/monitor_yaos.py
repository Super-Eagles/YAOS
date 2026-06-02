import os
import sys
import time
import subprocess
import json
import collections

# Path to save the log file
LOG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "yaos_monitor.log")

def get_logical_cores():
    try:
        return os.cpu_count() or 1
    except:
        return 1

CORES = get_logical_cores()

def log_message(msg):
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    formatted = f"[{timestamp}] {msg}"
    print(formatted)
    sys.stdout.flush()
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(formatted + "\n")
    except Exception as e:
        print(f"Failed to write to log file: {e}")

def run_command(cmd):
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, text=True, encoding="ansi")
        return res.stdout
    except Exception as e:
        return ""

def sample_process_times():
    # Query PID, Name, UserModeTime, KernelModeTime using WMIC
    # UserModeTime and KernelModeTime are in units of 100 nanoseconds
    cmd = "wmic process get ProcessId, Name, UserModeTime, KernelModeTime /format:csv"
    output = run_command(cmd)
    
    samples = {}
    for line in output.splitlines():
        line = line.strip()
        if not line or line.startswith("Node,") or "ProcessId" in line:
            continue
        parts = line.split(",")
        if len(parts) >= 5:
            try:
                # Format of CSV from WMIC process: Node, KernelModeTime, Name, ProcessId, UserModeTime
                kernel_time = int(parts[1])
                name = parts[2]
                pid = int(parts[3])
                user_time = int(parts[4])
                samples[pid] = {
                    "name": name,
                    "total_time": kernel_time + user_time
                }
            except (ValueError, IndexError):
                continue
    return samples

def get_top_cpu_processes(interval=1.0):
    t1 = time.time()
    s1 = sample_process_times()
    time.sleep(interval)
    t2 = time.time()
    s2 = sample_process_times()
    
    delta_time = t2 - t1
    if delta_time <= 0:
        return []
    
    # 1 unit of User/Kernel time = 100 nanoseconds = 1e-7 seconds
    # Total possible time per core in delta_time seconds = delta_time * 1e7 units
    total_possible_units = delta_time * 1e7 * CORES
    
    process_cpu = []
    for pid, data2 in s2.items():
        if pid in s1:
            data1 = s1[pid]
            delta_units = data2["total_time"] - data1["total_time"]
            if delta_units > 0:
                cpu_percent = (delta_units / total_possible_units) * 100.0
                process_cpu.append({
                    "pid": pid,
                    "name": data2["name"],
                    "cpu": round(cpu_percent, 2)
                })
                
    # Sort by CPU usage descending
    process_cpu.sort(key=lambda x: x["cpu"], reverse=True)
    return process_cpu[:10]

def get_process_handles_and_threads():
    # Query HandleCount, ThreadCount, ProcessId, Name of all active processes
    cmd = "wmic process get Name, ProcessId, HandleCount, ThreadCount /format:csv"
    output = run_command(cmd)
    
    processes_info = []
    for line in output.splitlines():
        line = line.strip()
        if not line or line.startswith("Node,") or "ProcessId" in line:
            continue
        parts = line.split(",")
        if len(parts) >= 5:
            try:
                # Format: Node, HandleCount, Name, ProcessId, ThreadCount
                handles = int(parts[1])
                name = parts[2]
                pid = int(parts[3])
                threads = int(parts[4])
                
                # We are interested in YAOS processes, Python, Node, and high handle counts
                processes_info.append({
                    "pid": pid,
                    "name": name,
                    "handles": handles,
                    "threads": threads
                })
            except (ValueError, IndexError):
                continue
    return processes_info

def get_connection_states():
    output = run_command("netstat -ano")
    
    states = collections.Counter()
    port_connections = collections.Counter()
    yaos_ports_connections = collections.Counter()
    
    # Common ports used by YAOS/FastNet (let's monitor them specifically if we see them)
    # netstat lines look like: TCP    127.0.0.1:port    127.0.0.1:peer_port    ESTABLISHED    PID
    for line in output.splitlines():
        line = line.strip()
        if not line or "Active Connections" in line or "Proto" in line:
            continue
        parts = line.split()
        if len(parts) >= 4:
            proto = parts[0]
            local_addr = parts[1]
            state = ""
            pid = ""
            
            # netstat columns vary based on protocol
            if proto.upper() == "TCP":
                if len(parts) == 5:
                    state = parts[3]
                    pid = parts[4]
                elif len(parts) == 4:
                    # UDP doesn't have state, but sometimes TCP lines are parsed differently
                    state = parts[2]
                    pid = parts[3]
                
                states[state] += 1
                
                # Track high-frequency local ports
                try:
                    local_port = int(local_addr.split(":")[-1])
                    port_connections[local_port] += 1
                except:
                    pass
            elif proto.upper() == "UDP":
                pid = parts[2] if len(parts) >= 3 else ""
                states["UDP"] += 1
                
    # Find top 3 local ports with most connections
    top_ports = port_connections.most_common(5)
    
    return {
        "states": dict(states),
        "top_ports": top_ports
    }

def main():
    log_message("=== YAOS System Performance Monitor Started ===")
    log_message(f"Detected {CORES} logical CPU cores.")
    log_message(f"Logging performance data to: {LOG_FILE}")
    log_message("Monitoring top CPU consumers, handle leaks, thread counts, and network connection states...")
    
    iteration = 0
    try:
        while True:
            iteration += 1
            log_message(f"--- Poll Iteration #{iteration} ---")
            
            # 1. Get top CPU consuming processes (incorporates a 1-second sleep)
            top_cpu = get_top_cpu_processes(interval=1.5)
            log_message("Top CPU-Consuming Processes:")
            for p in top_cpu:
                if p["cpu"] > 0.1:
                    log_message(f"  PID: {p['pid']:<6} | Name: {p['name']:<25} | CPU: {p['cpu']}%")
            
            # 2. Get process handle and thread counts
            proc_info = get_process_handles_and_threads()
            # Filter YAOS-related processes, Python, Node, and any process with > 1000 handles
            yaos_related = []
            leaks_suspected = []
            for p in proc_info:
                name_lower = p["name"].lower()
                is_yaos = "yaos" in name_lower or "fastnet" in name_lower
                is_subagent = "python" in name_lower or "node" in name_lower
                if is_yaos or is_subagent:
                    yaos_related.append(p)
                if p["handles"] > 1000 or p["threads"] > 80:
                    leaks_suspected.append(p)
                    
            log_message("YAOS & Subagent Process States:")
            if not yaos_related:
                log_message("  No YAOS/FastNet/Python/Node processes detected.")
            else:
                for p in yaos_related:
                    log_message(f"  PID: {p['pid']:<6} | Name: {p['name']:<25} | Threads: {p['threads']:<4} | Handles: {p['handles']:<6}")
                    
            if leaks_suspected:
                log_message("Suspicious Processes (High Thread/Handle counts):")
                # Sort by handles descending
                leaks_suspected.sort(key=lambda x: x["handles"], reverse=True)
                for p in leaks_suspected[:5]:
                    log_message(f"  PID: {p['pid']:<6} | Name: {p['name']:<25} | Threads: {p['threads']:<4} | Handles: {p['handles']:<6} [SUSPICIOUS]")
            
            # 3. Get network connection states
            conn_info = get_connection_states()
            log_message("Network Connection States:")
            states_str = ", ".join([f"{k}: {v}" for k, v in conn_info["states"].items()])
            log_message(f"  {states_str if states_str else 'No active TCP/UDP connections.'}")
            
            log_message("Top Active Local Ports:")
            for port, count in conn_info["top_ports"]:
                log_message(f"  Port: {port:<6} | Active Connections: {count}")
                
            # Log separation
            log_message("-" * 60 + "\n")
            
            # Sleep remainder of the 5-second interval (1.5 seconds was used in CPU sampling)
            time.sleep(3.5)
            
    except KeyboardInterrupt:
        log_message("=== YAOS System Performance Monitor Stopped by User ===")

if __name__ == "__main__":
    main()
