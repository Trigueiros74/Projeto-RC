Projeto: Event Booking (Servidor e Cliente em C)

Descrição:
- Este projecto contém um servidor (`server`) e um cliente/usuário (`user`) escritos em C.
- O servidor implementa a lógica principal (ficheiros em `server/`) e o cliente fornece comandos para interagir com o servidor (ficheiros em `user/`).

Requisitos:
- Compilador C (`gcc`) e utilitários de linha de comando padrão (make, sh).
- macOS / Linux recomendado.

Estrutura principal:
- server/: código-fonte do servidor e scripts de teste.
- user/: código-fonte do cliente/usuário.

Arquivos importantes (incluídos):
- server/Makefile, server/ES.c, server/parser_server.c, server/commands.c, server/commands.h, server/parser_server.h
- user/Makefile, user/user.c, user/parser_user.c, user/commands.c, user/commands.h, user/parser_user.h

Compilar:
- Compilar o servidor:
  cd server
  make

- Compilar o cliente/usuário:
  cd user
  make

Executar:
- Inicie o servidor primeiro (no directório `server`):
  ./ES

- Em seguida, no directório `user`, execute o cliente:
  ./user

Limpeza:
- Limpar ficheiros objecto e binários:
  cd server && make clean
  cd user && make clean

Observações adicionais:
- Ambos os programas são aplicações de linha de comando; não há interface gráfica.
- Se algum binário não for gerado com os nomes acima, confira o `Makefile` em cada directório (o alvo padrão produz `ES` em `server` e `user` em `user`).

Submissão:
- Inclua todos os ficheiros fornecidos nas pastas `server/` e `user/` juntamente com este `readme.txt`.

  Autor: João Ferreira ist1110573
  Autor: Eduardo Fernandes ist1110628
