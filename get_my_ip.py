#!/usr/bin/env python3
"""
Quick script to find your computer's IP address on the local network
"""

import socket

def get_local_ip():
    """Get the local IP address"""
    try:
        # Connect to an external address (doesn't actually send data)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "Could not determine IP"

if __name__ == "__main__":
    ip = get_local_ip()
    print("\n" + "=" * 50)
    print("Your computer's local IP address:")
    print(f"  {ip}")
    print("=" * 50)
    print("\nUpdate this line in dimmy-the-cube.ino:")
    print(f"  IPAddress pcIP({ip.replace('.', ', ')});")
    print("=" * 50 + "\n")
