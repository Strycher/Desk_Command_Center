"""Quick serial capture for debugging — captures N seconds from COM port."""
import serial
import time
import sys

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
DURATION = int(sys.argv[2]) if len(sys.argv) > 2 else 70
BAUD = 115200

print(f"Capturing {DURATION}s from {PORT} @ {BAUD}...")
ser = serial.Serial(PORT, BAUD, timeout=1)
ser.reset_input_buffer()
start = time.time()
lines = 0
while time.time() - start < DURATION:
    raw = ser.readline()
    if raw:
        try:
            text = raw.decode("utf-8", errors="replace").strip()
            elapsed = time.time() - start
            print(f"[{elapsed:6.1f}s] {text}")
            lines += 1
        except Exception as e:
            print(f"[decode err] {e}")
ser.close()
print(f"--- capture done: {lines} lines in {DURATION}s ---")
