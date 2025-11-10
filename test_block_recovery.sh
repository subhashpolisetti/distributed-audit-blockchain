#!/bin/bash

VENV_DIR="venv"

if [ ! -d "$VENV_DIR" ]; then
    echo "Creating virtual environment"
    python3 -m venv $VENV_DIR
fi

source $VENV_DIR/bin/activate

pip install cryptography grpcio protobuf

echo "Stopping all nodes"
./stop_nodes.sh
sleep 2

echo "Resetting block data files"
rm -rf data/node*/block_*.json
rm -rf data/node*/block_*.pb

echo "Starting nodes 1, 2, and 3"
./blockchain_node --config ../config.json --id node1 --port 50051 &
sleep 2
./blockchain_node --config ../config.json --id node2 --port 50052 &
sleep 2
./blockchain_node --config ../config.json --id node3 --port 50053 &
sleep 5

if [ ! -f "client/common_pb2.py" ] || [ ! -f "client/file_audit_pb2.py" ]; then
    echo "Generating Python gRPC code..."
    cd client
    python3 generate_proto.py
    cd ..
fi

if [ ! -f "client/private_key.pem" ] || [ ! -f "client/public_key.pem" ]; then
    echo "Generating RSA key pair"
    cd client
    python3 audit_client.py --generate-keys --file-id dummy --file-name dummy --user-id dummy --user-name dummy --access-type 1
    cd ..
fi

for i in {1..10}; do
    echo "Submitting audit $i"
    cd client
    python3 audit_client.py --server localhost:50051 --file-id file$i --file-name file$i.txt --user-id user1 --user-name "Test User" --access-type 1 --private-key private_key.pem --public-key public_key.pem
    cd ..
    sleep 1
done

echo "Waiting for blocks to be created and propagated"
sleep 30

echo "Checking latest block ID on node1"
LATEST_BLOCK=$(ls -1 data/node1/block_*.json 2>/dev/null | sort -V | tail -n 1)
if [ -z "$LATEST_BLOCK" ]; then
    echo "No blocks found on node1. Test failed."
    exit 1
fi 
BLOCK_ID=$(basename $LATEST_BLOCK | sed 's/block_\([0-9]*\)\.json/\1/')
echo "Latest block on node1: $BLOCK_ID"

echo "Starting node4 (was previously offline)"
./blockchain_node --config ../config.json --id node4 --port 50054 &
sleep 2

echo "Waiting for node4 to recover blocks"
sleep 60

echo "Checking if node4 has recovered the blocks"
NODE4_LATEST_BLOCK=$(ls -1 data/node4/block_*.json 2>/dev/null | sort -V | tail -n 1)
if [ -z "$NODE4_LATEST_BLOCK" ]; then
    echo "No blocks found on node4. Recovery failed."
else
    NODE4_BLOCK_ID=$(basename $NODE4_LATEST_BLOCK | sed 's/block_\([0-9]*\)\.json/\1/')
    echo "Latest block on node4: $NODE4_BLOCK_ID"
    
    if [ "$NODE4_BLOCK_ID" -eq "$BLOCK_ID" ]; then
        echo "Recovery successful .. Node4 has synced all blocks."
    else
        echo "Partial recovery"
    fi
fi

echo "Block files on all nodes"
echo "Node1:"
ls -la data/node1/block_*.json 2>/dev/null || echo "No blocks"
echo "Node2:"
ls -la data/node2/block_*.json 2>/dev/null || echo "No blocks"
echo "Node3:"
ls -la data/node3/block_*.json 2>/dev/null || echo "No blocks"
echo "Node4:"
ls -la data/node4/block_*.json 2>/dev/null || echo "No blocks"

./stop_nodes.sh

