#!/usr/bin/env python3

import sys
import time
import uuid
import json
import base64
import argparse
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives.serialization import (
    load_pem_private_key,
    load_pem_public_key,
    Encoding,
    PrivateFormat,
    PublicFormat,
    NoEncryption
)
import grpc


try:
    import common_pb2
    import file_audit_pb2
    import file_audit_pb2_grpc
    import block_chain_pb2
    import block_chain_pb2_grpc
except ImportError:
    print("Error: gRPC generated modules not found")
    sys.exit(1)

def generate_key_pair():
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048
    )
    public_key = private_key.public_key()
    
    
    private_pem = private_key.private_bytes(
        encoding=Encoding.PEM,
        format=PrivateFormat.PKCS8,
        encryption_algorithm=NoEncryption()
    ).decode('utf-8')
    
    public_pem = public_key.public_bytes(
        encoding=Encoding.PEM,
        format=PublicFormat.SubjectPublicKeyInfo
    ).decode('utf-8')
    
    return private_pem, public_pem

def save_keys(private_key, public_key, private_file="private_key.pem", public_file="public_key.pem"):
    with open(private_file, 'w') as f:
        f.write(private_key)
    
    with open(public_file, 'w') as f:
        f.write(public_key)
    
    print(f"Keys saved to {private_file} and {public_file}")

def load_keys(private_file="private_key.pem", public_file="public_key.pem"):
    try:
        with open(private_file, 'r') as f:
            private_key = f.read()
        
        with open(public_file, 'r') as f:
            public_key = f.read()
        
        return private_key, public_key
    except FileNotFoundError:
        print(f"Key files not found  Generating new keys")
        private_key, public_key = generate_key_pair()
        save_keys(private_key, public_key, private_file, public_file)
        return private_key, public_key

def sign_data(data, private_key_pem):
    private_key = load_pem_private_key(
        private_key_pem.encode('utf-8'),
        password=None
    )
    
    signature = private_key.sign(
        data.encode('utf-8'),
        padding.PKCS1v15(),
        hashes.SHA256()
    )
    
    return signature.hex()

def create_audit(file_id, file_name, user_id, user_name, access_type, private_key_pem, public_key_pem):
    req_id = str(uuid.uuid4())
    timestamp = int(time.time())
    
    audit_dict = {
        "req_id": req_id,
        "file_info": {"file_id": file_id, "file_name": file_name},
        "user_info": {"user_id": user_id, "user_name": user_name},
        "access_type": access_type,
        "timestamp": timestamp
    }
    
   
    data_to_sign = json.dumps(audit_dict, sort_keys=True, separators=(",", ":"))
    
    #debugging
    print("Data to sign:", data_to_sign)
    
   
    signature = sign_data(data_to_sign, private_key_pem)
    
   
    audit = common_pb2.FileAudit()
    audit.req_id = req_id
    audit.file_info.file_id = file_id
    audit.file_info.file_name = file_name
    audit.user_info.user_id = user_id
    audit.user_info.user_name = user_name
    audit.access_type = access_type
    audit.timestamp = timestamp
    audit.signature = signature
    audit.public_key = public_key_pem
    
    return audit

def submit_audit(server_address, audit):
  
    channel = grpc.insecure_channel(server_address)
    
    
    stub = file_audit_pb2_grpc.FileAuditServiceStub(channel)
    
  
    try:
        response = stub.SubmitAudit(audit)
        return response
    except grpc.RpcError as e:
        print(f"RPC Error: {e.code()}: {e.details()}")
        return None

def whisper_audit(server_address, audit):
    channel = grpc.insecure_channel(server_address)
    
    stub = block_chain_pb2_grpc.BlockChainServiceStub(channel)
    
    try:
        response = stub.WhisperAuditRequest(audit)
        return response
    except grpc.RpcError as e:
        print(f"RPC Error: {e.code()}: {e.details()}")
        return None

def get_latest_block(server_address):
    channel = grpc.insecure_channel(server_address)
    
    stub = block_chain_pb2_grpc.BlockChainServiceStub(channel)
    
    try:
        block_id = 1
        latest_block = None
        
        while True:
            request = block_chain_pb2.GetBlockRequest(id=block_id)
            response = stub.GetBlock(request)
            
            if response.status == "success":
                latest_block = response.block
                block_id += 1
            else:
                break
        
        return latest_block
    except grpc.RpcError as e:
        print(f"RPC Error: {e.code()}: {e.details()}")
        return None

def propose_block(server_address, audits, previous_block=None):
    channel = grpc.insecure_channel(server_address)
    
    stub = block_chain_pb2_grpc.BlockChainServiceStub(channel)
    
    if previous_block is None:
        previous_block = get_latest_block(server_address)
    
    block = block_chain_pb2.Block()
    
    if previous_block is None:
        block.id = 1
        block.previous_hash = ""
    else:
        block.id = previous_block.id + 1
        block.previous_hash = previous_block.hash
    
    for audit in audits:
        block.audits.append(audit)
    
    merkle_root = compute_merkle_root(audits)
    block.merkle_root = merkle_root
    
    block_hash = compute_block_hash(block)
    block.hash = block_hash
    
    try:
        response = stub.ProposeBlock(block)
        return response, block
    except grpc.RpcError as e:
        print(f"RPC Error: {e.code()}: {e.details()}")
        return None, block

def commit_block(server_address, block):
    channel = grpc.insecure_channel(server_address)
    
    stub = block_chain_pb2_grpc.BlockChainServiceStub(channel)
    
    try:
        response = stub.CommitBlock(block)
        return response
    except grpc.RpcError as e:
        print(f"RPC Error: {e.code()}: {e.details()}")
        return None

def compute_merkle_root(audits):
    if not audits:
        return ""
    
    hashes = []
    for audit in audits:
        audit_dict = {
            "req_id": audit.req_id,
            "file_info": {
                "file_id": audit.file_info.file_id,
                "file_name": audit.file_info.file_name
            },
            "user_info": {
                "user_id": audit.user_info.user_id,
                "user_name": audit.user_info.user_name
            },
            "access_type": audit.access_type,
            "timestamp": audit.timestamp
        }
        
        audit_str = json.dumps(audit_dict, sort_keys=True, separators=(",", ":"))
        
        audit_hash = hashlib.sha256(audit_str.encode()).hexdigest()
        hashes.append(audit_hash)
    
    while len(hashes) > 1:
        if len(hashes) % 2 == 1:
            hashes.append(hashes[-1])
        
        new_hashes = []
        for i in range(0, len(hashes), 2):
            combined = hashes[i] + hashes[i+1]
            new_hash = hashlib.sha256(combined.encode()).hexdigest()
            new_hashes.append(new_hash)
        
        hashes = new_hashes
    
    return hashes[0] if hashes else ""

def compute_block_hash(block):
    block_str = f"{block.id}:{block.previous_hash}:{block.merkle_root}"
    
    return hashlib.sha256(block_str.encode()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest='command')
    
    submit_parser = subparsers.add_parser('submit')
    submit_parser.add_argument('--server', default='localhost:50051')
    submit_parser.add_argument('--file-id', required=True)
    submit_parser.add_argument('--file-name', required=True)
    submit_parser.add_argument('--user-id', required=True)
    submit_parser.add_argument('--user-name', required=True)
    submit_parser.add_argument('--access-type', type=int, choices=[1, 2, 3, 4], required=True)
    submit_parser.add_argument('--private-key', default='private_key.pem')
    submit_parser.add_argument('--public-key', default='public_key.pem')
    submit_parser.add_argument('--generate-keys', action='store_true')
    
    whisper_parser = subparsers.add_parser('whisper')
    whisper_parser.add_argument('--server', default='localhost:50051')
    whisper_parser.add_argument('--file-id', required=True)
    whisper_parser.add_argument('--file-name', required=True)
    whisper_parser.add_argument('--user-id', required=True)
    whisper_parser.add_argument('--user-name', required=True)
    whisper_parser.add_argument('--access-type', type=int, choices=[1, 2, 3, 4], required=True)
    whisper_parser.add_argument('--private-key', default='private_key.pem')
    whisper_parser.add_argument('--public-key', default='public_key.pem')
    whisper_parser.add_argument('--generate-keys', action='store_true')
    
    propose_parser = subparsers.add_parser('propose')
    propose_parser.add_argument('--server', default='localhost:50051')
    propose_parser.add_argument('--num-audits', type=int, default=10)
    
    commit_parser = subparsers.add_parser('commit')
    commit_parser.add_argument('--server', default='localhost:50051')
    commit_parser.add_argument('--block-id', type=int, required=True)
    
    get_parser = subparsers.add_parser('get')
    get_parser.add_argument('--server', default='localhost:50051')
    get_parser.add_argument('--block-id', type=int, required=True)
    
    args = parser.parse_args()
    
    if args.command == 'submit':
        if args.generate_keys:
            private_key, public_key = generate_key_pair()
            save_keys(private_key, public_key, args.private_key, args.public_key)
        else:
            private_key, public_key = load_keys(args.private_key, args.public_key)
        
        audit = create_audit(
            args.file_id,
            args.file_name,
            args.user_id,
            args.user_name,
            args.access_type,
            private_key,
            public_key
        )
        
        print(f"Created audit with req_id: {audit.req_id}")
        
        print(f"Submitting audit to {args.server}...")
        response = submit_audit(args.server, audit)
        
        if response:
            print(f"Response: {response.status}")
            if response.status == "success":
                print("Audit submitted successfully!")
            else:
                print(f"Error: {response.error_message}")
    
    elif args.command == 'whisper':
        if args.generate_keys:
            private_key, public_key = generate_key_pair()
            save_keys(private_key, public_key, args.private_key, args.public_key)
        else:
            private_key, public_key = load_keys(args.private_key, args.public_key)
        
        audit = create_audit(
            args.file_id,
            args.file_name,
            args.user_id,
            args.user_name,
            args.access_type,
            private_key,
            public_key
        )
        
        print(f"Created audit with req_id: {audit.req_id}")
        
        print(f"Whispering audit to {args.server}...")
        response = whisper_audit(args.server, audit)
        
        if response:
            print(f"Response: {response.status}")
            if response.status == "success":
                print("Audit whispered successfully")
            else:
                print(f"Error: {response.error_message}")
    
    elif args.command == 'propose':
        audits = []
        for i in range(args.num_audits):
            private_key, public_key = load_keys()
            
            audit = create_audit(
                f"file_{i}",
                f"document_{i}.txt",
                f"user_{i}",
                f"User {i}",
                1,  # READ
                private_key,
                public_key
            )
            
            audits.append(audit)
        
        print(f"Proposing block with {len(audits)} audits to {args.server}...")
        response, block = propose_block(args.server, audits)
        
        if response:
            print(f"Response: {response.vote}")
            if response.vote:
                print("Block proposal approved!")
                print(f"Block ID: {block.id}")
                print(f"Block hash: {block.hash}")
                
                print("Committing block...")
                commit_response = commit_block(args.server, block)
                
                if commit_response:
                    print(f"Commit response: {commit_response.status}")
                    if commit_response.status == "success":
                        print("Block committed successfully!")
                    else:
                        print(f"Error: {commit_response.error_message}")
            else:
                print(f"Block proposal rejected: {response.error_message}")
    
    elif args.command == 'commit':
        channel = grpc.insecure_channel(args.server)
        stub = block_chain_pb2_grpc.BlockChainServiceStub(channel)
        
        request = block_chain_pb2.GetBlockRequest(id=args.block_id)
        response = stub.GetBlock(request)
        
        if response.status == "success":
            block = response.block
            
            print(f"Committing block {block.id} to {args.server}...")
            commit_response = commit_block(args.server, block)
            
            if commit_response:
                print(f"Commit response: {commit_response.status}")
                if commit_response.status == "success":
                    print("Block committed successfully!")
                else:
                    print(f"Error: {commit_response.error_message}")
        else:
            print(f"Error getting block: {response.error_message}")
    
    elif args.command == 'get':
        channel = grpc.insecure_channel(args.server)
        stub = block_chain_pb2_grpc.BlockChainServiceStub(channel)
        
        request = block_chain_pb2.GetBlockRequest(id=args.block_id)
        response = stub.GetBlock(request)
        
        if response.status == "success":
            block = response.block
            
            print(f"Block ID: {block.id}")
            print(f"Block hash: {block.hash}")
            print(f"Previous hash: {block.previous_hash}")
            print(f"Merkle root: {block.merkle_root}")
            print(f"Number of audits: {len(block.audits)}")
            
            for i, audit in enumerate(block.audits):
                print(f"Audit {i+1}:")
                print(f"  Req ID: {audit.req_id}")
                print(f"  File ID: {audit.file_info.file_id}")
                print(f"  File name: {audit.file_info.file_name}")
                print(f"  User ID: {audit.user_info.user_id}")
                print(f"  User name: {audit.user_info.user_name}")
                print(f"  Access type: {audit.access_type}")
                print(f"  Timestamp: {audit.timestamp}")
        else:
            print(f"Error getting block: {response.error_message}")
    
    else:
        pass

if __name__ == "__main__":
    import hashlib 
    main()
