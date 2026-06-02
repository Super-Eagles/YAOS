import subprocess
import os

def main():
    # Query conhost.exe and all other processes to build a process tree mapping
    cmd = 'wmic process get ProcessId, ParentProcessId, Name, CommandLine /format:csv'
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, text=True, encoding='ansi')
    
    processes = {}
    conhosts = []
    
    for line in res.stdout.splitlines():
        line = line.strip()
        if not line or line.startswith('Node,') or 'ProcessId' in line:
            continue
        parts = line.split(',')
        if len(parts) >= 5:
            try:
                # CSV format from WMIC: Node, CommandLine, Name, ParentProcessId, ProcessId
                cmdline = parts[1]
                name = parts[2]
                parent_pid = int(parts[3])
                pid = int(parts[4])
                
                processes[pid] = {
                    'name': name,
                    'parent_pid': parent_pid,
                    'cmdline': cmdline
                }
                if name.lower() == 'conhost.exe':
                    conhosts.append(pid)
            except (ValueError, IndexError):
                continue

    print(f"=== Found {len(conhosts)} active 'Console Window Host' (conhost.exe) processes ===\n")
    
    for pid in conhosts:
        info = processes[pid]
        parent_pid = info['parent_pid']
        parent_name = "Unknown"
        parent_cmdline = "N/A"
        
        if parent_pid in processes:
            parent_name = processes[parent_pid]['name']
            parent_cmdline = processes[parent_pid]['cmdline']
            
        print(f"conhost.exe (PID: {pid})")
        print(f"  └─ Opened by parent process: {parent_name} (PID: {parent_pid})")
        print(f"  └─ Parent Command Line: {parent_cmdline}")
        print("-" * 70)

if __name__ == '__main__':
    main()
