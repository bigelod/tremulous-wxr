#!/usr/bin/env python3
# Test UDP spoofer for PC without headset
# Sends fake HMD pose to 127.0.0.1:7872
import socket, time, math
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# 28 floats: L_q 4, L_stick 2, L_pos 3, R_q 4, R_stick 2, R_pos 3, HMD_q 4, HMD_pos 3, IPD, FOV_H, FOV_V, SYNC
# example from SixDOFinator
ipd=0.063
fov_h=99.0
fov_v=103.4
sync=0
while True:
    # simple yaw rotation
    yaw = (time.time()*0.2) % (2*math.pi)
    qx=0; qy=math.sin(yaw/2); qz=0; qw=math.cos(yaw/2)
    # L/R zero
    packet = f"0 0 0 1 0 0 0 0 0  0 0 0 1 0 0 0 0 0  {qx} {qy} {qz} {qw} 0 0 0 {ipd} {fov_h} {fov_v} {sync} TFFFFFFFFFFFFFFFFFFFF"
    sock.sendto(packet.encode(), ("127.0.0.1", 7872))
    sync = (sync+1) % 256
    time.sleep(1/72.0)
