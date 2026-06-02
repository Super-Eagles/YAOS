"""
CPU 占用监控脚本
用法：python scripts/cpu_monitor.py [--threshold 30] [--interval 2] [--top 10] [--watch yaos,dwm]

功能：
- 实时显示 CPU 占用最高的进程
- 超过阈值时自动记录快照到 tmp/cpu_spike_*.txt
- --watch 指定额外关注的进程名（逗号分隔），无论排名都会显示
- 持续监控直到 Ctrl+C
"""

import argparse
import datetime
import os
import sys
import time

try:
    import psutil
except ImportError:
    print("缺少 psutil，正在安装...")
    os.system(f"{sys.executable} -m pip install psutil")
    import psutil


def format_bytes(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} TB"


def get_all_processes():
    procs = []
    for p in psutil.process_iter(["pid", "name", "cpu_percent", "memory_info",
                                   "status", "cmdline", "num_threads"]):
        try:
            info = p.info
            cpu = info.get("cpu_percent") or 0.0
            mem = info.get("memory_info")
            mem_mb = mem.rss / 1024 / 1024 if mem else 0.0
            cmd = " ".join(info.get("cmdline") or [])[:120]
            procs.append({
                "pid": info["pid"],
                "name": info["name"] or "",
                "cpu": cpu,
                "mem_mb": mem_mb,
                "status": info.get("status", ""),
                "threads": info.get("num_threads", 0),
                "cmd": cmd,
            })
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    return procs


def snapshot_text(procs, total_cpu, total_mem, watch_names=None):
    lines = []
    ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines.append(f"=== CPU 快照  {ts} ===")
    lines.append(f"系统 CPU: {total_cpu:.1f}%   内存: {total_mem.percent:.1f}%  "
                 f"已用 {format_bytes(total_mem.used)} / {format_bytes(total_mem.total)}")
    lines.append("")
    lines.append(f"{'PID':>7}  {'CPU%':>6}  {'MEM(MB)':>9}  {'线程':>5}  {'状态':>8}  {'进程名':<28}  命令行")
    lines.append("-" * 120)

    # Top 进程
    top = sorted(procs, key=lambda x: x["cpu"], reverse=True)[:10]
    shown_pids = set()
    for p in top:
        shown_pids.add(p["pid"])
        lines.append(
            f"{p['pid']:>7}  {p['cpu']:>6.1f}  {p['mem_mb']:>9.1f}  "
            f"{p['threads']:>5}  {p['status']:>8}  {p['name']:<28}  {p['cmd']}"
        )

    # 额外关注的进程（不在 Top 10 里也显示）
    if watch_names:
        watch_lower = [w.lower() for w in watch_names]
        watched = [p for p in procs
                   if p["pid"] not in shown_pids
                   and any(w in p["name"].lower() for w in watch_lower)]
        if watched:
            lines.append("")
            lines.append("--- 关注进程 ---")
            for p in sorted(watched, key=lambda x: x["cpu"], reverse=True):
                lines.append(
                    f"{p['pid']:>7}  {p['cpu']:>6.1f}  {p['mem_mb']:>9.1f}  "
                    f"{p['threads']:>5}  {p['status']:>8}  {p['name']:<28}  {p['cmd']}"
                )

    return "\n".join(lines)


def save_snapshot(text, out_dir="tmp"):
    os.makedirs(out_dir, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    path = os.path.join(out_dir, f"cpu_spike_{ts}.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return path


def clear_line(n=1):
    for _ in range(n):
        sys.stdout.write("\033[F\033[K")


def main():
    parser = argparse.ArgumentParser(description="CPU 占用监控")
    parser.add_argument("--threshold", type=float, default=30.0,
                        help="单进程 CPU 超过此值(%%)时保存快照，默认 30")
    parser.add_argument("--interval", type=float, default=2.0,
                        help="采样间隔秒数，默认 2")
    parser.add_argument("--top", type=int, default=10,
                        help="显示前 N 个进程，默认 10")
    parser.add_argument("--watch", type=str, default="yaos,dwm,yaos_tests",
                        help="额外关注的进程名（逗号分隔），默认 yaos,dwm,yaos_tests")
    parser.add_argument("--no-save", action="store_true",
                        help="不保存快照文件，只打印")
    args = parser.parse_args()

    watch_names = [w.strip() for w in args.watch.split(",") if w.strip()] if args.watch else []

    print(f"监控中... 阈值={args.threshold}%  间隔={args.interval}s  Top={args.top}")
    if watch_names:
        print(f"额外关注: {', '.join(watch_names)}")
    print("按 Ctrl+C 退出\n")

    # 预热
    psutil.cpu_percent(interval=None)
    for p in psutil.process_iter(["cpu_percent"]):
        try:
            p.cpu_percent(interval=None)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass

    last_lines = 0
    spike_count = 0

    try:
        while True:
            time.sleep(args.interval)

            total_cpu = psutil.cpu_percent(interval=None)
            total_mem = psutil.virtual_memory()
            procs = get_all_processes()

            if last_lines > 0:
                clear_line(last_lines)

            text = snapshot_text(procs, total_cpu, total_mem, watch_names)
            print(text)
            last_lines = text.count("\n") + 1

            spikes = [p for p in procs if p["cpu"] >= args.threshold
                      and "System Idle" not in p["name"]]
            if spikes and not args.no_save:
                spike_count += 1
                path = save_snapshot(text)
                msg = f"\n⚠️  发现 {len(spikes)} 个高 CPU 进程！快照: {path}"
                print(msg)
                last_lines += msg.count("\n") + 1

    except KeyboardInterrupt:
        print(f"\n监控结束，共保存 {spike_count} 个快照。")


if __name__ == "__main__":
    main()
