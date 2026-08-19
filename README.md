<div align="center">

# 🎟️ Event Reservation Platform

**Servidor e cliente de reserva de eventos sobre sockets UDP e TCP, em C**

Projeto de *Redes de Computadores* (RC) — Instituto Superior Técnico, 2025/26 · LEIC Alameda

![C](https://img.shields.io/badge/language-C99-00599C?style=flat-square&logo=c)
![Sockets](https://img.shields.io/badge/sockets-UDP%20%2B%20TCP-1f6feb?style=flat-square)
![Build](https://img.shields.io/badge/build-Wall%20Wextra%20sem%20avisos-brightgreen?style=flat-square)

</div>

---

## 📖 Sobre o projeto

Plataforma de reserva de eventos composta por dois programas que comunicam
pela rede:

* **`ES`** — o *Event-reservation Server*. Escuta no mesmo porto em UDP e em
  TCP, guarda utilizadores, eventos e reservas em disco, e transfere os
  ficheiros que descrevem cada evento.
* **`user`** — a aplicação cliente. Lê comandos do teclado e traduz cada um
  para a mensagem de protocolo correspondente.

Qualquer utilizador registado pode criar eventos (com um cartaz, um PDF ou
outro ficheiro à escolha, até 10 MB), fechá-los, consultar os eventos
existentes e reservar lugares. O enunciado completo está em
[enunciado-projeto-RC.md](enunciado-projeto-RC.md).

---

## 🚀 Compilação e execução

```console
$ make
```

Os executáveis `ES` e `user` ficam na diretoria atual. Cada programa também
pode ser compilado isoladamente a partir de `server/` ou de `client/`.

```console
$ ./ES [-p ESport] [-v]           # servidor; por omissão o porto 58068
$ ./user [-n ESIP] [-p ESport]    # cliente;  por omissão localhost:58068
```

| Opção | Significado |
|:---:|:---|
| `-p` | Porto onde o servidor aceita pedidos, em UDP e em TCP |
| `-v` | Modo verboso: descreve cada pedido recebido e a sua origem |
| `-n` | Endereço ou nome da máquina onde o servidor corre |

O porto por omissão é `58000 + 68`, sendo 68 o número do grupo. O servidor tem
de ser iniciado primeiro; cria a diretoria `ESDIR/` a partir de onde for
executado, e é aí que guarda tudo.

```console
$ make clean       # remove objetos e executáveis
$ make distclean   # remove também o ESDIR/ com os dados do servidor
```

---

## ⚙️ Comandos do cliente

| Comando | Sintaxe | Transporte | Ação |
|:---|:---|:---:|:---|
| `login` | `login <UID> <password>` | UDP | Autentica ou regista o utilizador |
| `changePass` | `changePass <antiga> <nova>` | TCP | Altera a palavra-passe |
| `logout` | `logout` | UDP | Termina a sessão |
| `unregister` | `unregister` | UDP | Elimina a conta |
| `create` | `create <nome> <ficheiro> <dd-mm-aaaa> <hh:mm> <lugares>` | TCP | Cria um evento e envia o ficheiro que o descreve |
| `close` | `close <EID>` | TCP | Deixa de aceitar reservas |
| `myevents` \| `mye` | `myevents` | UDP | Eventos criados pelo utilizador |
| `list` | `list` | TCP | Todos os eventos |
| `show` | `show <EID>` | TCP | Detalhes do evento e descarga do ficheiro |
| `reserve` | `reserve <EID> <lugares>` | TCP | Reserva lugares |
| `myreservations` \| `myr` | `myreservations` | UDP | As 50 reservas mais recentes |
| `exit` | `exit` | — | Sai da aplicação (exige `logout` primeiro) |

O `UID` são 6 dígitos, a palavra-passe são 8 caracteres alfanuméricos e o `EID`
são 3 dígitos. `help` mostra esta lista dentro da aplicação.

O `create` lê o ficheiro a partir da diretoria onde o cliente está a correr, e
o `show` guarda-o nessa mesma diretoria.

### Exemplo

```console
$ ./ES -v &
ES listening on port 58068 (UDP and TCP), verbose mode

$ ./user
login 110573 secret42
New user registered. UID: 110573
create Gala cartaz.png 20-12-2027 21:30 120
Event created. EID: 001
reserve 001 4
Reservation accepted: 4 seats in event 001.
list
Events:
  001  Gala        20-12-2027 21:30  (open)
show 001
Event 001
  name       Gala
  owner      110573
  date       20-12-2027 21:30
  seats      4 reserved out of 120
  file       cartaz.png (192833 bytes)
  stored in  /home/user/rc
logout
Logout successful.
exit
```

Do lado do servidor, `-v` regista cada pedido com a origem, o utilizador e o
estado devolvido:

```text
[UDP 127.0.0.1:51214] LIN uid=110573 -> REG
[TCP 127.0.0.1:57982] CRE uid=110573 -> OK
[TCP 127.0.0.1:57985] RID uid=110573 -> ACC
```

---

## 📡 Protocolo

Os campos são separados por **um único espaço** e cada mensagem termina em
`\n`. Um pedido com sintaxe inválida recebe o estado `ERR`.

### UDP — gestão de utilizadores e listagens curtas

| Pedido | Resposta | Estados |
|:---|:---|:---|
| `LIN UID pass` | `RLI status` | `OK` `NOK` `REG` `ERR` |
| `LOU UID pass` | `RLO status` | `OK` `NOK` `UNR` `WRP` `ERR` |
| `UNR UID pass` | `RUR status` | `OK` `NOK` `UNR` `WRP` `ERR` |
| `LME UID pass` | `RME status[ EID estado]*` | `OK` `NOK` `NLG` `WRP` `ERR` |
| `LMR UID pass` | `RMR status[ EID data lugares]*` | `OK` `NOK` `NLG` `WRP` `ERR` |

### TCP — eventos, reservas e transferência de ficheiros

| Pedido | Resposta | Estados |
|:---|:---|:---|
| `CRE UID pass nome data lugares Fname Fsize Fdata` | `RCE status [EID]` | `OK` `NOK` `NLG` `WRP` `ERR` |
| `CLS UID pass EID` | `RCL status` | `OK` `NOK` `NLG` `NOE` `EOW` `SLD` `PST` `CLO` `ERR` |
| `LST` | `RLS status[ EID nome estado data]*` | `OK` `NOK` `ERR` |
| `SED EID` | `RSE status [UID nome data lugares reservados Fname Fsize Fdata]` | `OK` `NOK` `ERR` |
| `RID UID pass EID lugares` | `RRI status [n_lugares]` | `ACC` `REJ` `CLS` `SLD` `PST` `NLG` `WRP` `NOK` `ERR` |
| `CPS UID antiga nova` | `RCP status` | `OK` `NID` `NLG` `NOK` `ERR` |

### Estado de um evento

| Código | Significado |
|:---:|:---|
| `0` | A data do evento já passou |
| `1` | Aberto a reservas |
| `2` | No futuro, mas esgotado |
| `3` | Fechado pelo criador |

---

## 📁 Estrutura do projeto

```text
.
├── Makefile                   Compila ES e user para a raiz do repositório
├── server/
│   ├── ES.c                   Sockets, select(), fork() e leitura das mensagens
│   ├── parser_server.c        Validação dos campos e das mensagens recebidas
│   ├── commands.c             Lógica de cada comando do protocolo
│   ├── storage.c              Estado persistente em disco e exclusão mútua
│   └── Makefile
├── client/
│   ├── user.c                 Ciclo de leitura de comandos e sessão do utilizador
│   ├── parser_user.c          Validação do que o utilizador escreve
│   ├── commands.c             Diálogo com o servidor e transferência de ficheiros
│   └── Makefile
└── enunciado-projeto-RC.md    Enunciado do projeto
```

---

## 🏗️ Arquitetura

```text
                       ┌──────── select() ────────┐
                       │                          │
                  socket UDP                 socket TCP
                       │                          │
              resposta imediata              accept() → fork()
                       │                          │
                       └────────┬─────────────────┘
                                ▼
                       flock(ESDIR/.lock)
                                ▼
                          ESDIR/ (disco)
```

Um `select()` espera pelos dois sockets. Os datagramas são curtos e são
respondidos no próprio processo; cada ligação TCP é entregue a um processo
filho, para que o envio de um anexo de 10 MB não deixe o pedido seguinte à
espera.

O servidor não guarda estado em memória entre pedidos: cada operação lê do
disco o que precisa e escreve de volta o que alterou. Os processos partilham
apenas a árvore `ESDIR/`, e todos os manipuladores delimitam o seu trabalho com
um `flock()` — partilhado para os comandos que só leem, exclusivo para os que
escrevem. É esse bloqueio que impede que duas reservas simultâneas contem o
mesmo lugar duas vezes.

Os ficheiros que descrevem os eventos nunca são carregados inteiros em
memória: são copiados em blocos entre o disco e o socket, nos dois sentidos e
nos dois programas.

### Armazenamento

```text
ESDIR/
├── .lock                                    tranca consultiva (flock)
├── USERS/<UID>/
│   ├── <UID>_pass.txt                       existir == registado
│   ├── <UID>_login.txt                      existir == com sessão iniciada
│   ├── CREATED/<EID>.txt                    índice dos eventos criados
│   └── RESERVED/R-<UID>-<data>-<seq>.txt    reservas feitas
└── EVENTS/<EID>/
    ├── START_<EID>.txt                      dono, nome, ficheiro, lugares, data
    ├── END_<EID>.txt                        existir == fechado
    ├── RES_<EID>.txt                        lugares já reservados
    ├── DESCRIPTION/<Fname>                  o ficheiro que descreve o evento
    └── RESERVATIONS/R-<UID>-<data>-<seq>.txt
```

O nome de cada ficheiro de reserva codifica o instante em que foi feita, pelo
que ordenar os nomes por ordem decrescente é o suficiente para responder ao
`myreservations` com as 50 reservas mais recentes.

---

## 📄 Licença

O código-fonte é distribuído sob a licença [MIT](LICENSE).

O `enunciado-projeto-RC.md` é material didático do Instituto Superior Técnico,
incluído apenas como contexto, e não é abrangido por essa licença.

---

## 👥 Autores

**ist1110573** (João Ferreira)
**ist1110628** (Eduardo Fernandes)

Instituto Superior Técnico · Redes de Computadores 2025/26 · Grupo 68
