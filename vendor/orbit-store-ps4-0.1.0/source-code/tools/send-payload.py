"""Explicit, one-shot ELF transfer. Never invoked by the build or test commands."""
import argparse
import hashlib
from pathlib import Path
import socket

parser = argparse.ArgumentParser(description='Send an ELF to an already-running, supported PS4/PS5 loader.')
parser.add_argument('payload', type=Path)
parser.add_argument('--host', required=True)
parser.add_argument('--port', type=int, default=9021)
parser.add_argument('--transfer-timeout', type=float, default=120,
                    help='Socket timeout during the ELF transfer, in seconds (default: 120).')
parser.add_argument('--confirm-console-execution', action='store_true')
args = parser.parse_args()
if not args.confirm_console_execution:
    parser.error('Console execution needs explicit owner confirmation; no connection was made.')
data = args.payload.read_bytes()
if data[:4] != b'\x7fELF':
    parser.error('Input is not an ELF file; no connection was made.')
if not 1 <= args.port <= 65535:
    parser.error('Invalid port; no connection was made.')
if not 0 < args.transfer_timeout <= 600:
    parser.error('Transfer timeout must be between 0 and 600 seconds; no connection was made.')
print(f'Artifact SHA-256: {hashlib.sha256(data).hexdigest()}', flush=True)
with socket.create_connection((args.host, args.port), timeout=15) as connection:
    connection.settimeout(args.transfer_timeout)
    connection.sendall(data)
    connection.shutdown(socket.SHUT_WR)
print(f'Sent {len(data):,} bytes. Successful execution still needs console verification.')
