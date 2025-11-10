#!/bin/bash

stop_node_by_pid_file() {
    local node_id=$1
    local pid_file="node$node_id.pid"
    
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        echo "Stopping node$node_id (PID: $pid) using PID file"
        
        if kill -15 $pid 2>/dev/null; then
            echo "Node$node_id stopped"
        else
            echo "Node$node_id was not running"
        fi
        
        rm -f "$pid_file"
    else
        echo "No PID file found for node$node_id"
    fi
}  

stop_node_by_process() {
    local node_id=$1
    local port=$2
    
    local pids=$(ps aux | grep "blockchain_node.*--id node$node_id.*--address localhost:$port" | grep -v grep | awk '{print $2}')
    
    if [ -n "$pids" ]; then
        for pid in $pids; do
            echo "Stopping node$node_id (PID: $pid) found by process search"
            kill -15 $pid 2>/dev/null
            echo "Node$node_id (PID: $pid) stopped."
        done
    else
        echo "No running process found for node$node_id on port $port"
    fi
}

stop_node_by_port() {
    local port=$1
    
    if command -v lsof >/dev/null 2>&1; then
        local pids=$(lsof -t -i :$port 2>/dev/null)
        
        if [ -n "$pids" ]; then
            for pid in $pids; do
                echo "Stopping process (PID: $pid) listening on port $port"
                kill -9 $pid 2>/dev/null
                echo "Process on port $port stopped"
            done
        else
            echo "No process found listening on port $port"
        fi
    else
        echo "skipping port-based process termination"
    fi
}

stop_node_by_pid_file 1
stop_node_by_pid_file 2
stop_node_by_pid_file 3
stop_node_by_pid_file 4
stop_node_by_pid_file 5

stop_node_by_process 1 50051
stop_node_by_process 2 50052
stop_node_by_process 3 50053
stop_node_by_process 4 50054
stop_node_by_process 5 50055


stop_node_by_port 50051
stop_node_by_port 50052
stop_node_by_port 50053
stop_node_by_port 50054
stop_node_by_port 50055


remaining_pids=$(ps aux | grep "blockchain_node" | grep -v grep | awk '{print $2}')
if [ -n "$remaining_pids" ]; then
    for pid in $remaining_pids; do
        echo "Stopping remaining blockchain_node process (PID: $pid)"
        kill -9 $pid 2>/dev/null
    done
    echo "All remaining blockchain_node processes stopped"
else
    echo "No remaining blockchain_node processes found"
fi

echo "All nodes stopped"
