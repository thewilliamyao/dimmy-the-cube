#!/usr/bin/env python3
"""
Wireless Monitor for Dimmy the Cube
Receives and displays sensor data via UDP
"""

import socket
import sys
from datetime import datetime

# Configuration
UDP_PORT = 4211  # Must match pcUdpPort in Arduino code

def main():
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", UDP_PORT))

    print("=" * 60)
    print("🎲 DIMMY THE CUBE - Wireless Monitor")
    print("=" * 60)
    print(f"Listening for data on UDP port {UDP_PORT}...")
    print("Press Ctrl+C to stop\n")

    try:
        while True:
            data, addr = sock.recvfrom(1024)
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            message = data.decode('utf-8').strip()

            # Clear line and print data
            print(f"[{timestamp}] {message}")

    except KeyboardInterrupt:
        print("\n\nStopping monitor...")
        sock.close()
        sys.exit(0)
    except Exception as e:
        print(f"\nError: {e}")
        sock.close()
        sys.exit(1)

if __name__ == "__main__":
    main()
