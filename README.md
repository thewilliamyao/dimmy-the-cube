# Dimmy the Cube - Wireless Setup Guide

## Quick Start (5 Steps)

### 1. Find Your PC's IP Address
```bash
python3 get_my_ip.py
```
This will show something like: `192.168.1.100`

### 2. Configure WiFi + PC IP
Edit `dimmy-the-cube.ino`:

**Lines 11-12** - Your WiFi:
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

**Line 19** - Your PC's IP (from step 1):
```cpp
IPAddress pcIP(192, 168, 1, 100);  // Use YOUR PC's IP here
```

### 3. First Upload (USB)
1. Connect ESP32 via USB
2. Upload the code
3. Open Serial Monitor (115200 baud)
4. Verify WiFi connects and sensors initialize

### 4. Start Wireless Monitor
In a new terminal, run:
```bash
python3 monitor_dimmy.py
```

You should see:
```
🎲 DIMMY THE CUBE - Wireless Monitor
Listening for data on UDP port 4211...

[14:23:45.123] Floor:3 | Dimmest:2 (1234) | Brightest:5 (8765)
[14:23:45.173] Floor:3 | Dimmest:2 (1201) | Brightest:5 (8801)
```

### 5. Go Wireless!
Unplug USB, power with battery - cube streams data to your PC wirelessly! 🎉

## Pin Outs

### ESP32 Pins

| GPIO | Function |
|------|----------|
| 16 | Wire SDA → PCA9548 multiplexer + APDS9960 sensors |
| 17 | Wire SCL → PCA9548 multiplexer + APDS9960 sensors |
| 21 | Wire1 SDA → AS5600 encoder (control motor) |
| 22 | Wire1 SCL → AS5600 encoder (control motor) |
| 25 | SimpleFOCmini IN3 (control motor) |
| 26 | SimpleFOCmini EN (control motor) |
| 27 | ESC signal (main drive motor) |
| 32 | SimpleFOCmini IN1 (control motor) |
| 33 | SimpleFOCmini IN2 (control motor) |

### TCA9548A I2C Multiplexer (0x70)

| Channel | Device |
|---------|--------|
| 0 | ❌ Unused |
| 1 | APDS9960 — side 0 |
| 2 | APDS9960 — side 1 |
| 3 | APDS9960 — side 2 |
| 4 | ❌ Unused (not wired) |
| 5 | APDS9960 — side 3 |
| 6 | APDS9960 — side 4 |
| 7 | APDS9960 — side 5 |

### Notes
- AS5600 encoder uses a dedicated I2C bus (Wire1) to avoid contention with the APDS9960 sensors
- AS5600 VCC must go to **3.3V**, not VIN — 5V will put it in a broken state where it returns 0xFFF
- Opposite side pairs: 0↔5, 1↔3, 2↔4
- Side with highest proximity reading = floor side

## Serial Motor Test Commands

When `#define WIFI_ENABLED` is commented out, you can control the motors directly
from the Serial Monitor (115200 baud) for testing:

| Command | Effect |
|---------|--------|
| `c<deg>` | Set control motor angle (closed-loop), e.g. `c0`, `c90`, `c45`. Auto-enables motor. |
| `e<us>` | Set ESC microseconds, e.g. `e1500` (neutral), `e1925` (forward), `e1075` (reverse) |
| `o<rad/s>` | Open-loop velocity test (no encoder), e.g. `o5`, `o-5`, `o0` |
| `x<V>` | Raw driver test — directly spins motor at given voltage for 3s, e.g. `x4`, `x8` |
| `n` | Enable control motor (holding torque ON) |
| `f` | Disable control motor (free-spinning, saves battery) |

## Power Savings Strategy

The gimbal motor holding torque is the biggest idle power draw (~300–400mA at 8V).
To preserve battery during the show:

- Control motor is `disable()`d at boot and during the `SENSING` state
- `triggerMove()` re-enables it just before positioning
- `BRAKING → SENSING` transition disables it again
- Keep `#define WIFI_ENABLED` commented out during the show (saves ~80–150mA)

## What Gets Monitored

| Value | Description |
|-------|-------------|
| **floorSide** | Which side (0-5) is on the floor (highest proximity) |
| **dimmestSide** | Which side has the least light |
| **brightestSide** | Which side has the most light |

Numbers in parentheses show actual light values (0-65535)

## Wireless Code Updates (OTA)

Next time you need to update code:
1. Arduino IDE → **Tools → Port**
2. Select network port: `dimmy-cube at 192.168.1.XXX`
3. Click **Upload** - no cable needed!
4. Password: `dimmy123` (if prompted)

## Two Ways to Monitor

| Method | When to Use |
|--------|-------------|
| **USB Serial Monitor** | Initial testing, debugging, when cube is plugged in |
| **Wireless (UDP)** | Playtesting with free movement, untethered operation |

Both show the same data - use whichever works for your workflow!

## Playtesting Workflow

```
1. Power cube with battery pack
2. Run: python3 monitor_dimmy.py
3. Walk around testing different lighting conditions
4. See real-time data on your PC screen
5. Need code changes? Upload wirelessly via OTA!
```

## Troubleshooting

**Monitor script shows nothing:**
- Check PC IP is correct in Arduino code (line 19)
- Make sure both PC and cube are on same WiFi
- Try running `python3 get_my_ip.py` again to verify IP

**Can't see network port in Arduino IDE:**
- Ensure ESP32 and PC are on same WiFi
- Wait 30 seconds after powering cube
- Check Serial Monitor for cube's IP address

**OTA upload fails:**
- Password is `dimmy123`
- Restart Arduino IDE
- Power cycle the cube

**Firewall blocking UDP:**
- Mac: System Preferences → Security & Privacy → Firewall → Allow Python
- Windows: Allow Python through Windows Firewall
