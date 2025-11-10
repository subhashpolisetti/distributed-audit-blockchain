#!/usr/bin/env python3

import os
import sys
import subprocess

def main():
    
    script_dir = os.path.dirname(os.path.abspath(__file__))
   
    proto_dir = os.path.join(os.path.dirname(script_dir), 'proto')
    
    
    if not os.path.exists(proto_dir):
        print(f"Error: proto directory not found: {proto_dir}")
        return 1
    
    required_files = ['common.proto', 'file_audit.proto', 'block_chain.proto']
    for file in required_files:
        file_path = os.path.join(proto_dir, file)
        if not os.path.exists(file_path):
            print(f"Error: proto file not found: {file_path}")
            return 1
    
    
    
    cmd = [
        sys.executable, '-m', 'grpc_tools.protoc',
        f'-I{proto_dir}',
        f'--python_out={script_dir}',
        f'--grpc_python_out={script_dir}',
        os.path.join(proto_dir, 'common.proto'),
        os.path.join(proto_dir, 'file_audit.proto'),
        os.path.join(proto_dir, 'block_chain.proto')
    ]
    
    try:
        subprocess.check_call(cmd)
        print("Python gRPC code generated successfully")
        return 0
    except subprocess.CalledProcessError as e:
        print(f"Error generating gRPC code: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
