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
except ImportError:
    print("Error: grpc generated modules not found")
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
        print(f"Key files not found. Generating new keys...")
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
    except NameError as e:
        print(f"NameError: {e}")
        return None

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', default='localhost:50051')
    parser.add_argument('--file-id', required=True)
    parser.add_argument('--file-name', required=True)
    parser.add_argument('--user-id', required=True)
    parser.add_argument('--user-name', required=True)
    parser.add_argument('--access-type', type=int, choices=[1, 2, 3, 4], required=True)
    parser.add_argument('--private-key', default='private_key.pem')
    parser.add_argument('--public-key', default='public_key.pem')
    parser.add_argument('--generate-keys', action='store_true')
    
    args = parser.parse_args()
    
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
    
if __name__ == "__main__":
    main()
