import socket
import os

HOST = '127.0.0.1'
PORT = 58068
UDP_ADDR = (HOST, PORT)

def udp(cmd, timeout=1.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    try:
        print(f">>> UDP SEND: {cmd.strip()}")
        s.sendto(cmd.encode(), UDP_ADDR)
        data, _ = s.recvfrom(1024)
        reply = data.decode().strip()
        print(f"<<< UDP RECV: {reply}\n")
        return reply
    except socket.timeout:
        print("<<< UDP TIMEOUT\n")
        return "TIMEOUT"
    finally:
        s.close()

def tcp(cmd, data=b"", timeout=2.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        print(f">>> TCP SEND: {cmd.strip()}")
        s.connect((HOST, PORT))
        s.sendall(cmd.encode())
        if data:
            s.sendall(data)

        buf = b""
        while not buf.endswith(b"\n"):
            c = s.recv(1)
            if not c:
                break
            buf += c

        header = buf.decode().strip()
        print(f"<<< TCP RECV: {header}\n")
        return header
    except socket.timeout:
        print("<<< TCP TIMEOUT\n")
        return "TIMEOUT"
    finally:
        s.close()

# ------------------------------------------------------------------
# TEST USERS (5 USERS)
# ------------------------------------------------------------------
OWNER = ("100001", "pass0001")
USER1 = ("100002", "pass0002")
USER2 = ("100003", "pass0003")

print("\n=== 1. LOGIN USERS ===\n")
udp(f"LIN {OWNER[0]} {OWNER[1]}\n")
udp(f"LIN {USER1[0]} {USER1[1]}\n")
udp(f"LIN {USER2[0]} {USER2[1]}\n")

# ------------------------------------------------------------------
# CREATE EVENT
# ------------------------------------------------------------------
# ------------------------------------------------------------------
# CREATE EVENT (OWNER)
# ------------------------------------------------------------------
print("\n=== 2. CREATE EVENT (OWNER) ===\n")

FILENAME = "test.txt"
FILE_CONTENT = b"Small event description\n"

# create the file locally (IMPORTANT)
with open(FILENAME, "wb") as f:
    f.write(FILE_CONTENT)

file_size = os.path.getsize(FILENAME)

with open(FILENAME, "rb") as f:
    file_data = f.read()

cmd = (
    f"CRE {OWNER[0]} {OWNER[1]} "
    f"TestEvt 01-01-2030 12:00 10 "
    f"{FILENAME} {file_size}\n"
)

resp = tcp(cmd, file_data)

if "OK" not in resp:
    print("Event creation failed, aborting tests.")
    exit(1)

EID = resp.split()[-1]
print(f"Captured EID = {EID}\n")


# ------------------------------------------------------------------
# RESERVATIONS
# ------------------------------------------------------------------
print("\n=== 3. RESERVE SEATS ===\n")
tcp(f"RID {USER1[0]} {USER1[1]} {EID} 3\n")
tcp(f"RID {USER2[0]} {USER2[1]} {EID} 5\n")

print("\n=== 4. OVERBOOK (EXPECT REJ) ===\n")
tcp(f"RID {USER1[0]} {USER1[1]} {EID} 5\n")

# ------------------------------------------------------------------
# SHOW EVENT FILE
# ------------------------------------------------------------------
print("\n=== 5. SHOW EVENT DESCRIPTION ===\n")
tcp(f"SED {EID}\n")

# ------------------------------------------------------------------
# CLOSE EVENT
# ------------------------------------------------------------------
print("\n=== 6. CLOSE EVENT ===\n")
tcp(f"CLS {OWNER[0]} {OWNER[1]} {EID}\n")

print("\n=== 7. RESERVE AFTER CLOSE (EXPECT CLS) ===\n")
tcp(f"RID {USER1[0]} {USER1[1]} {EID} 1\n")

# ------------------------------------------------------------------
# LOGOUT
# ------------------------------------------------------------------
print("\n=== 8. LOGOUT USERS ===\n")
udp(f"LOU {OWNER[0]} {OWNER[1]}\n")
udp(f"LOU {USER1[0]} {USER1[1]}\n")
udp(f"LOU {USER2[0]} {USER2[1]}\n")

print("\n=== TESTS FINISHED ===\n")
