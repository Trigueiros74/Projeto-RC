import socket
import threading
import time
import random
import string
import sys
import os
import struct
from concurrent.futures import ThreadPoolExecutor, as_completed

# --- CONFIGURAÇÃO ---
HOST = '127.0.0.1'
PORT = 58068
UDP_ADDR = (HOST, PORT)

# Volumes de Teste
FUZZ_ITERATIONS = 2000      
STRESS_CLIENTS = 100        
STRESS_LOOPS = 5            

# Cores
PASS = '\033[92m[PASS]\033[0m'
FAIL = '\033[91m[FAIL]\033[0m'
WARN = '\033[93m[WARN]\033[0m'
INFO = '\033[94m[INFO]\033[0m'
RESET = '\033[0m'

stats = {'total': 0, 'pass': 0, 'fail': 0}
stats_lock = threading.Lock()

def record(condition, msg):
    with stats_lock:
        stats['total'] += 1
        if condition:
            stats['pass'] += 1
        else:
            stats['fail'] += 1
            print(f"{FAIL} {msg}")

# --- CLIENTE GENÉRICO ---
class Client:
    def udp(self, cmd, timeout=1.0):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(timeout)
        try:
            s.sendto(cmd.encode(), UDP_ADDR)
            d, _ = s.recvfrom(65536)
            return d.decode(errors='ignore').strip()
        except socket.timeout: return "TIMEOUT"
        except Exception as e: return f"ERROR: {e}"
        finally: s.close()

    def tcp(self, cmd_header, data=None, timeout=2.0):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        try:
            s.connect((HOST, PORT))
            s.sendall(cmd_header.encode())
            if data: s.sendall(data)
            
            buf = b""
            while True:
                c = s.recv(1)
                if not c: break
                buf += c
                if c == b'\n': break
            header = buf.decode(errors='ignore').strip()
            
            body = b""
            if cmd_header.startswith("SED") and header.startswith("RSE OK"):
                try:
                    fsize = int(header.split()[-1])
                    while len(body) < fsize:
                        chunk = s.recv(min(4096, fsize - len(body)))
                        if not chunk: break
                        body += chunk
                except: pass
            return header, body
        except socket.timeout: return "TIMEOUT", None
        except Exception as e: return f"ERROR: {e}", None
        finally: s.close()

# --- GERADORES ---
def rnd_uid(kind='valid'):
    if kind=='valid': return "".join(random.choices(string.digits, k=6))
    if kind=='short': return "".join(random.choices(string.digits, k=5))
    if kind=='long': return "".join(random.choices(string.digits, k=7))
    if kind=='alpha': return "12A456"
    return ""

def rnd_pass(kind='valid'):
    chars = string.ascii_letters + string.digits
    if kind=='valid': return "".join(random.choices(chars, k=8))
    if kind=='short': return "short1"
    if kind=='long': return "toolong12"
    if kind=='symbol': return "pass.123"
    return ""

# ==============================================================================
# BATERIA 1: FUZZING MASSIVO E BOUNDARY TESTING
# ==============================================================================
def test_boundaries_and_fuzz():
    print(f"\n{INFO} === 1. FUZZING & BOUNDARIES ({FUZZ_ITERATIONS} Iterações) ===")
    c = Client()
    
    # 1.1 UIDs e Passwords (UDP)
    for _ in range(FUZZ_ITERATIONS):
        uid = rnd_uid(random.choice(['short', 'long', 'alpha', 'valid']))
        pw = rnd_pass(random.choice(['short', 'long', 'symbol', 'valid']))
        is_valid_fmt = (len(uid)==6 and uid.isdigit()) and (len(pw)==8 and pw.isalnum())
        
        res = c.udp(f"LIN {uid} {pw}")
        
        if not is_valid_fmt:
            record("ERR" in res, f"LIN Formato Inválido rejeitado ({uid}:{pw}) -> {res}")
        else:
            record(res in ["RLI REG", "RLI OK", "RLI NOK"], f"LIN Formato Válido aceite ({uid}:{pw}) -> {res}")

    # 1.2 Nomes de Ficheiros e Eventos (TCP)
    valid_u, valid_p = "900000", "tester00"
    c.udp(f"LIN {valid_u} {valid_p}")
    
    edge_cases = [
        ("EventOK", "file.txt", "RCE OK"),       # CORRIGIDO: Nome com <10 chars
        ("EventNameTooLong", "file.txt", "RCE ERR"), # Nome > 10
        ("Ev-ent", "file.txt", "RCE ERR"),       # Símbolo no nome
        ("Event", "toolongfilename123456789.txt", "RCE ERR"), # Fname > 24
        ("Event", "file", "RCE ERR"),            # Sem extensão
        ("Event", "file.tx", "RCE ERR"),         # Extensão curta
        ("Event", "fi#le.txt", "RCE ERR"),       # Símbolo no fname
        ("Event", "valid.txt", "RCE ERR"),       # Size 0 simulado
    ]
    
    for ename, fname, expect in edge_cases:
        sz = 10 if expect == "RCE OK" else 0 
        cmd = f"CRE {valid_u} {valid_p} {ename} 01-01-2030 10:00 50 {fname} {sz}\n"
        res, _ = c.tcp(cmd, b"X"*sz)
        record(expect in res or (sz==0 and "ERR" in res), f"CRE Edge Case '{ename}'/'{fname}' -> {res}")

# ==============================================================================
# BATERIA 2: LÓGICA DE ESTADO E PERMISSÕES
# ==============================================================================
def test_business_logic():
    print(f"\n{INFO} === 2. LÓGICA DE NEGÓCIO E ESTADOS ===")
    owner = Client()
    hacker = Client()
    
    ou, op = "888001", "owner123"
    hu, hp = "888002", "hacker00"
    
    owner.udp(f"LIN {ou} {op}")
    hacker.udp(f"LIN {hu} {hp}")
    
    fname = "poster.txt"
    cmd = f"CRE {ou} {op} LogicEvt 01-01-2030 12:00 10 {fname} 5\n"
    res, _ = owner.tcp(cmd, b"DATA1")
    if "OK" not in res: return
    eid = res.split()[-1]
    
    res, _ = hacker.tcp(f"CLS {hu} {hp} {eid}\n")
    record("EOW" in res, f"Hacker impedido de fechar (EOW) -> {res}")
    
    res, _ = hacker.tcp(f"RID {hu} {hp} {eid} 11\n")
    record("REJ" in res, f"Reserva excessiva rejeitada (REJ) -> {res}")
    
    res, _ = owner.tcp(f"CLS {ou} {op} {eid}\n")
    record("OK" in res, f"Dono fechou evento -> {res}")
    
    res, _ = hacker.tcp(f"RID {hu} {hp} {eid} 1\n")
    record("CLS" in res, f"Reserva em evento fechado rejeitada (CLS) -> {res}")
    
    res, _ = hacker.tcp(f"CLS {hu} {hp} {eid}\n")
    record("EOW" in res, f"Hacker impedido novamente (EOW) -> {res}")
    
    res, _ = owner.tcp(f"CLS {ou} {op} {eid}\n")
    record("CLO" in res, f"Duplo fecho detetado (CLO) -> {res}")

# ==============================================================================
# BATERIA 3: INTEGRIDADE BINÁRIA E BUFFER OVERFLOW
# ==============================================================================
def test_binary_safety():
    print(f"\n{INFO} === 3. SEGURANÇA BINÁRIA E BUFFERS ===")
    c = Client()
    u, p = "777001", "binary01"
    c.udp(f"LIN {u} {p}")
    
    payload = b"\x00\x01\x02\x03\n\r\n\x00Start\x00End\xFF\xFE" * 100
    fsize = len(payload)
    
    cmd = f"CRE {u} {p} BinEvent 01-01-2035 10:00 100 binary.dat {fsize}\n"
    res, _ = c.tcp(cmd, payload)
    
    if "OK" in res:
        eid = res.split()[-1]
        hdr, body = c.tcp(f"SED {eid}\n")
        record(body == payload, "Integridade binária verificada (Upload == Download)")
    else:
        record(False, f"Falha no upload binário: {res}")

    giant_name = "A" * 5000
    cmd = f"CRE {u} {p} {giant_name} 01-01-2035 10:00 100 f.txt 1\n"
    res, _ = c.tcp(cmd, b"X")
    record(res != "TIMEOUT", "Servidor sobreviveu a ataque de Buffer Overflow TCP")

# ==============================================================================
# BATERIA 4: CONCORRÊNCIA E RACE CONDITIONS
# ==============================================================================
def test_concurrency_armageddon():
    print(f"\n{INFO} === 4. ARMAGEDDON DE CONCORRÊNCIA ({STRESS_CLIENTS} Clientes) ===")
    
    admin = Client()
    au, ap = "999999", "admin123"
    admin.udp(f"LIN {au} {ap}")
    
    for i in range(STRESS_LOOPS):
        fname = f"race_{i}.txt"
        res, _ = admin.tcp(f"CRE {au} {ap} Race{i} 01-01-2040 12:00 10 {fname} 1\n", b"X")
        if "OK" not in res: continue
        eid = res.split()[-1]
        
        results = []
        
        def worker(tid):
            c = Client()
            u = f"50{tid:04d}" 
            p = "passRace"
            c.udp(f"LIN {u} {p}")
            r, _ = c.tcp(f"RID {u} {p} {eid} 1\n")
            return r

        with ThreadPoolExecutor(max_workers=STRESS_CLIENTS) as executor:
            futures = [executor.submit(worker, t) for t in range(STRESS_CLIENTS)]
            for f in as_completed(futures):
                results.append(f.result())
        
        acc = sum(1 for r in results if "ACC" in r)
        
        if acc == 10:
            record(True, f"Race {i}: 10 Vendas EXATAS. Perfeito.")
        else:
            record(False, f"Race {i} FALHOU! Vendas: {acc} (Esperado 10).")

# --- MAIN ---
if __name__ == "__main__":
    if Client().udp("TEST") == "TIMEOUT":
        print(f"{FAIL} SERVIDOR DOWN. Inicia './ES -v' primeiro!")
        sys.exit(1)
        
    print(f"{INFO} A INICIAR ARMAGEDDON SUITE (VERSÃO CORRIGIDA)...")
    t_start = time.time()
    
    try:
        test_boundaries_and_fuzz()
        test_business_logic()
        test_binary_safety()
        test_concurrency_armageddon()
    except KeyboardInterrupt:
        print("\nCancelado.")
        
    dur = time.time() - t_start
    print(f"\n{INFO} === RESULTADO FINAL ===")
    print(f"Tempo: {dur:.2f}s")
    print(f"Testes Totais: {stats['total']}")
    print(f"{PASS} Passaram: {stats['pass']}")
    print(f"{FAIL} Falharam: {stats['fail']}")
    
    if stats['fail'] == 0:
        print(f"{PASS} SUCESSO ABSOLUTO! O servidor é indestrutível.")
    else:
        print(f"{WARN} O servidor tem vulnerabilidades.")