

## REDES DE COMPUTADORES
## 3
o
## ANO LEIC 2025/2026-2
o
## P
## GUIA DE PROJECTO24NOV2025 20:00
1    Servidor ES - Organiza ̧c ̃ao de dados
O servidor ES instalado na m ́aquina ‘tejo’ do laborat ́orio LT5 usa uma estrutura de directorias para armazenar toda a informa ̧c ̃ao referente
ao protocolo de forma persistente entre sess ̃oes.
Os alunos podem replicar total ou parcialmente a estrutura de directorias aqui descrita, a qual foi concebida tendo em vista a simplifica ̧c ̃ao
do processamento para armazenamento flex ́ıvel e indexa ̧c ̃ao de eventos e reservas, aproveitando ainda as estruturas de dados previstas no
filesystemdo Linux para obter facilmente a ordena ̧c ̃ao de ficheiros nele contidos.
O presente guia exemplifica tamb ́em, em linguagem C, a obten ̧c ̃ao de conte ́udos de directorias ordenados e convers ̃oes de tempos e datas
́uteis para a realiza ̧c ̃ao do projecto.
1.1    A directoria de trabalho do servidor ES
Na  directoria  de  trabalho  do  servidor  ES  encontram-se  duas  directorias  designadas  USERS  e  EVENTS.  Assume-se  que  no  arranque  do
servidor ES estas duas directorias j ́a est ̃ao criadas.
Ilustra-se a seguir a estrutura da base de dados do servidor, em que ESDIR  ́e a directoria onde se encontra o execut ́avel ES.
## 1

## ESDIR
## USERS
## (uid1)
## (uid1)pass.txt
## (uid1)login.txt
## CREATED
## (eid1).txt
## (eid2).txt
## RESERVED
## R-(uid1)-(date1).txt
## R-(uid1)-(date2).txt
## R-(uid1)-(date3).txt
## (uid2)
## (uid3)
## EVENTS
## (eid1)
START(eid1).txt
RES(eid1).txt
## DESCRIPTION
## (descfname1)
END(eid1).txt
## RESERVATIONS
## R-(uid1)-(date1).txt
## R-(uid2)-(date2).txt
## R-(uid3)-(date3).txt
## (eid2)
## (eid3)
## 2

O  servidor  ES  criar ́a  dentro  da  directoria  USERS  uma  directoria  por  cada  utilizador  que  se  regista.   A  designa ̧c ̃ao  da  directoria  de
utilizador coincide com o UID do utilizador em causa.  Do mesmo modo, o servidor ES criar ́a dentro da directoria EVENTS uma directoria
por cada evento criado.  A designa ̧c ̃ao da directoria de evento coincide com o EID do evento em causa.
- Na directoria de cada utilizador (sob USERS) s ̃ao criados:
•Um ficheiro (uid)pass.txt que cont ́em a password do utilizador.  Este ficheiro existir ́a enquanto o utilizador permanecer registado.
•Um ficheiro (uid)login.txt indicando que o utilizador est ́a em sess ̃ao.  Este ficheiro existe apenas enquanto o utilizador estiver em
sess ̃ao.
•Uma directoria designada CREATED contendo informa ̧c ̃ao sobre todos os eventos criados pelo utilizador.  A cada evento iniciado pelo
utilizador corresponde um ficheiro dentro da directoria CREATED.
•Uma directoria designada RESERVED contendo informa ̧c ̃ao sobre todos os eventos nos quais o utilizador reservou lugares.  A cada
evento  no  qual  o  utilizador  efectuou  uma  reserva,  corresponde  um  ficheiro  dentro  da  directoria  RESERVED.  Um  duplicado  deste
ficheiro existe tamb ́em sob a directoria RESERVATIONS de cada evento.
Quando um utilizador remove o seu registo (comandounregister), os ficheiros (uid)
pass.txt e (uid)login.txt e apenas estess ̃ao removidos,
preservando-se as respectivas directorias e o seu conte ́udo.
Se o utilizador voltar a registar-se, ser ́a criado um novo ficheiro (uid)pass.txt, assumindo-se que no novo registo, o utilizador herdar ́a toda
a informa ̧c ̃ao guardada nas directorias CREATED e RESERVED em momentos anteriores.  O efeito pr ́atico resultante de um utilizador
cancelar o seu registo e voltar a registar-se  ́e equivalente `a altera ̧c ̃ao da password.
Os ficheiros guardados nas directorias CREATED e RESERVED destinam-se a permitir ao servidor recuperar de forma r ́apida a lista de
todos os eventos relevantes para cada utilizador evitando buscas exaustivas sobre a directoria EVENTS, contendo esta  ́ultima informa ̧c ̃ao
mais completa embora organizada de outra forma.
- Na directoria de cada evento (sob EVENTS)  ́e criada uma directoria por cada evento criado.  Cada directoria de evento conter ́a:
## 3

•Um ficheiro START(eid).txt que cont ́em toda a informa ̧c ̃ao relevante sobre o evento.  Este ficheiro  ́e criado no momento de cria ̧c ̃ao
do evento quando o utilizador emite o comandocreate.
•Um ficheiro RES(eid).txt que cont ́em o n ́umero total de reservas acumuladas para o evento.  Este ficheiro  ́e criado com valor 0 quando
o utilizador emite o comandocreate.
•Um directoria DESCRIPTION que cont ́em o ficheiro descfname o qual cont ́em uma imagem ou uma descri ̧c ̃ao do evento criado.  Este
ficheiro  ́e carregado por TCP para o servidor quando o utilizador emite o comandocreate.
•Um ficheiro END(eid).txt que s ́o  ́e criado quando o evento  ́e dado como encerrado.
•Uma directoria RESERVATIONS que cont ́em todas as reservas efectuadas para o evento em causa.  A directoria RESERVATIONS
conter ́a um ficheiro por cada opera ̧c ̃ao de reserva de lugares.  O nome de cada ficheiro de reserva cont ́em o UID do utilizador que fez a
reserva e a data de recep ̧c ̃ao da reserva no servidor para permitir a recupera ̧c ̃ao de todas as reservas ordenadas por data.  Os ficheiros
contidos na directoria RESERVATIONS s ̃ao c ́opias dos ficheiros contidos nas directorias RESERVED dosusersque efectuaram reservas
no evento em causa.
1.2    Formatos dos ficheiros que contˆem informa ̧c ̃ao relevante
Descreve-se aqui o conte ́udo dos ficheiros da base de dados.  Os ficheiros
login.txt e aqueles que est ̃ao na directoria CREATED, valem por
apenas estarem presentes, independentemente da informa ̧c ̃ao que possam conter.
Descreve-se a seguir a organiza ̧c ̃ao da informa ̧c ̃ao relevante contida nos restantes ficheiros:
O ficheiro
pass.txt cont ́em a password do utilizador.  Se o utilizador emitir o comandounregister, o ficheiropass.txt  ́e apagado.
O ficheiro START
cont ́em uma  ́unica linha com os seguintes campos:
UID event
name descfname eventattend startdate starttime
## 4

em que:
UID  ́e a identifica ̧c ̃ao do utilizador com formato fixo de seis caracteres num ́ericos.
name  ́e o nome dado pelo utilizador ao evento em causa.
desc
fname  ́e o nome do ficheiro de descri ̧c ̃ao do evento.
event
attend  ́e o n ́umero total de lugares do evento.
startdate  ́e a data de in ́ıcio do evento no formato dd-mm-yyyy HH:MM
Se existir um ficheiro END
, ele indica que o evento se encontra encerrado.
Nesse caso, o ficheiro ENDcont ́em uma linha  ́unica com os seguintes campos:
enddatetime
em que:
end
datetime  ́e a data de encerramento do evento no formato dd-mm-yyyy HH:MM:SS
Os  ficheiros  contidos  na  directoria  RESERVED  tˆem  a  mesma  designa ̧c ̃ao  e  conte ́udo  dos  ficheiros  que  se  encontram  sob  a  directoria
RESERVATIONS. O nome destes ficheiros  ́e:
## R-(uid)-(date)(time).txt
Em que (uid)  ́e o UID do utilizador que efectuou a reserva, (date)  ́e a data em que foi efectuada a reserva no formato YYYY-MM-DD e
(time)  ́e a hora da reserva no formato HHMMSS. Se por absurdo um utilizador humano conseguir emitir duas reservas com um intervalo
inferior  a  um  segundo,  o  segundo  ficheiro  gerado  ir ́a  sobrepˆor-se  ao  primeiro  se  ambos  tiverem  a  mesma  designa ̧c ̃ao.   No  entanto  isso
n ̃ao constitui problema pois os ficheiros sob as directorias RESERVED e RESERVATIONS tˆem valor meramente informativo.  O ficheiro
RES(eid).txt cont ́em o real totalizador das reservas para um evento.
O conteudo de cada ficheiro de reserva  ́e:
## 5

UID resnum resdatetime
em que:
UID  ́e a identifica ̧c ̃ao do utilizador que efectuou a reserva.
res
num  ́e o n ́umero de lugares reservados.
res
datetime  ́e a data da reserva em causa no formato DD-MM-YYYY HH:MM:SS.
O  estado  de  um  evento  n ̃ao  se  encontra  gravado  em  ficheiro  algum.   Sempre  que  necess ́ario,  o  servidor  ES  calcula  o  estado  do  evento
a partir dos factores que o determinam.
O  ficheiro  END
.txt  que  assinala  a  termina ̧c ̃ao  de  um  evento   ́e  criado  sempre  que  o  utilizador  que  criou  o  evento  decide  fech ́a-lo  ou
sempre que o servidor, em acesso `a base de dados percebe que a data do evento se encontra no passado.  No primeiro caso de termina ̧c ̃ao, a
data de fecho do evento  ́e a data real na qual o evento foi encerrado.  No segundo caso, a data de fecho do evento  ́e a sua data de ocorrˆencia.
1.3    Exemplo de execu ̧c ̃ao
Para clarificar a organiza ̧c ̃ao de dados apresenta-se a seguir um exemplo de interac ̧c ̃ao entre aplica ̧c ̃oes clientesusere o servidorES.
-  O  utilizador  111111  come ̧cou  por  registar-se  emitindo  o  comando  login  pela  primeira  vez.   Deu  ent ̃ao  in ́ıcio  a  um  evento  com  a  des-
igna ̧c ̃ao ‘Conference’ carregando o ficheiro ‘Conference
01.jpg’ e estipulando para este evento uma data de ocorrˆencia 05-12-2025 15:00.  O
servidor ES notificou a aplica ̧c ̃ao user do sucesso da opera ̧c ̃ao e atribuiu ao evento acabado de criar a identifica ̧c ̃ao 001.
- Posteriormente, numa outra aplica ̧c ̃aouser, o utilizador 222222 registou-se emitindo o comando login pela primeira vez, e reservou 100
lugares no evento com EID==001.
A base de dados do servidor ES ficou com o seguinte conte ́udo:
## 6

## ESDIR
## USERS
## 111111
## 111111pass.txt
## 111111login.txt
## CREATED
## 001.txt
## RESERVED
## 222222
## 222222pass.txt
## 222222login.txt
## CREATED
## RESERVED
## R-222222-20251120121505.txt
## EVENTS
## 001
START001.txt
RES001.txt
## DESCRIPTION
## Conference01.jpg
## RESERVATIONS
## R-222222-20251120121505.txt
Ap ́os esta interac ̧c ̃ao, o ficheiro RES001.txt cont ́em o valor 100.
A dada altura,  o utilizador 222222 abandonou a sess ̃ao e o utilizador 333333 registou-se,  tendo logo de seguida reservado 50 lugares no
evento 001.  Ap ́os a emiss ̃ao dessa reserva, o utilizador 111111 decidiu encerrar o evento antecipadamente.  Ent ̃ao o conte ́udo da base de
dados passou a ser:
## 7

## ESDIR
## USERS
## 111111
## 111111pass.txt
## 111111login.txt
## CREATED
## 001.txt
## RESERVED
## 222222
## 222222pass.txt
## CREATED
## RESERVED
## R-222222-20251120121505.txt
## 333333
## 333333pass.txt
## 333333login.txt
## CREATED
## RESERVED
## R-333333-20251120121510.txt
## EVENTS
## 001
START001.txt
RES001.txt
## DESCRIPTION
## Conference01.jpg
END001.txt
## RESERVATIONS
## R-222222-20251120121505.txt
## R-333333-20251120121510.txt
No final destas interac ̧c ̃oes, o ficheiro RES
001.txt cont ́em o valor 150.
## 8

2    Complementos de programa ̧c ̃ao
Nesta sec ̧c ̃ao sugerem-se procedimentos e exemplifica-se a utiliza ̧c ̃ao de algumas fun ̧c ̃oes em C com a finalidade de facilitar a implementa ̧c ̃ao
do projecto.
2.1    Sobre o encerramento dos eventos
O servidor ES em opera ̧c ̃ao no ‘tejo’ n ̃ao toma a decis ̃ao de encerrar os eventos a determinados momentos.  N ̃ao usa portanto temporizadores
ou varrimentos peri ́odicos `a base de dados para encerrar os eventos.
Em vez disso, ele aproveita as interac ̧c ̃oes geradas pelas aplica ̧c ̃oes clientes que lhe acedem de forma concorrente, para verificar se um dado
evento deve ser encerrado.  Quando o servidor solicitado para uma dada opera ̧c ̃ao que acede a um evento, verifica que j ́a passou a data de
encerramento para esse evento, cria na respectiva directoria o ficheiro END
.  Se o utilizador que criou o evento, decide encerr ́a-lo, ent ̃ao o
servidor ES, no cumprimento dessa ordem de encerramento antecipado, encerra o evento criando o ficheiro END
## .
Se por exemplo, um utilizador pede o detalhe sobre um dado evento, o servidor aproveita essa solicita ̧c ̃ao (mensagem SED por TCP) para
verificar se o evento ultrapassou o prazo de validade e em caso afirmativo, encerra o evento criando o ficheiro END
mas enviando sempre o
detalhe sobre o evento ao utilizador que o solicitou.  Neste exemplo, o utilizador que solicitou o detalhe n ̃ao se apercebe que o servidor ES
aproveitou o seu pedido referente ao evento em causa para verificar se o prazo do mesmo tinha expirado.
Se um utilizador emitiu uma opera ̧c ̃ao de RID para um dado evento, o servidor ES verifica em primeiro lugar se o evento j ́a foi marcado
como encerrado.  Caso contr ́ario, verifica de seguida se o evento j ́a ultrapassou o seu prazo e responde em conformidade.  Caso isso aconte ̧ca,
marca  o  evento  como  encerrado.   Caso  o  evento  esteja  dentro  do  prazo,  cria  os  ficheiros  respectivos  nas  directorias  RESERVATIONS  e
RESERVED e responde afirmativamente ao utilizador.
Os  alunos  podem  usar  o  procedimento  acima  descrito  para  controlar  o  estado  dos  eventos,  ou  podem  usar  outro  que  entendam  conve-
niente como seja por exemplo a programa ̧c ̃ao de temporizadores no ES orientada para esse fim.
## 9

2.2    Cria ̧c ̃ao de directorias e subdirectorias
Exemplo de cria ̧c ̃ao de directoria e subdirectorias para novo evento:
i n t   CreateEVENTDir ( i n t   EID )
## {
c h a r   EIDdirname [ 1 5 ] ;
c h a r   RESERV
dirname [ 2 5 ] ;
c h a r   DESCdirname [ 2 5 ] ;
i n t   r e t ;
i f ( EID<1||EID>999)
r e t u r n ( 0 ) ;
s p r i n t f ( EIDdirname , ”EVENTS/%03d ” , EID ) ;
r e t=mkdir ( EIDdirname , 0 7 0 0 ) ;
i f ( r e t ==−1)
r e t u r n ( 0 ) ;
s p r i n t f ( RESERV
dirname , ”EVENTS/%03d/RESERVATIONS” , EID ) ;
r e t=mkdir ( RESERVdirname , 0 7 0 0 ) ;
i f ( r e t ==−1)
## {
r m d i r ( EID
dirname ) ;
r e t u r n ( 0 ) ;
## }
## 10

s p r i n t f ( DESCdirname , ”EVENTS/%03d/DESCRIPTION” , EID ) ;
r e t=mkdir ( DESC
dirname , 0 7 0 0 ) ;
i f ( r e t ==−1)
## {
r m d i r ( EID
dirname ) ;
r m d i r ( RESERV
dirname ) ;
r e t u r n ( 0 ) ;
## }
r e t u r n ( 1 ) ;
## }
2.3    Cria ̧c ̃ao e elimina ̧c ̃ao de ficheiro
Exemplos de fun ̧c ̃oes de cria ̧c ̃ao e elimina ̧c ̃ao do ficheiro de login:
i n t   C r e a t e L o g i n ( c h a r∗UID )
## {
c h a r   l o g i n
n a m e [ 3 5 ] ;
FILE∗f p ;
i f ( s t r l e n ( UID ) ! = 6 )
r e t u r n ( 0 ) ;
s p r i n t f ( l o g i nn a m e , ” USERS/%s/% sl o g i n . t x t ” , UID , UID ) ;
f p=f o p e n ( l o g i n
n a m e , ”w ” ) ;
i f ( f p==NULL)
r e t u r n ( 0 ) ;
f p r i n t f ( f p , ” Logged   i n\n ” ) ;
f c l o s e ( f p ) ;
r e t u r n ( 1 ) ;
## 11

## }
i n t   E r a s e L o g i n ( c h a r∗UID )
## {
c h a r   l o g i nn a m e [ 3 5 ] ;
i f ( s t r l e n ( UID ) ! = 6 )
r e t u r n ( 0 ) ;
s p r i n t f ( l o g i n
n a m e , ” USERS/%s/% sl o g i n . t x t ” , UID , UID ) ;
u n l i n k ( l o g i nn a m e ) ;
r e t u r n ( 1 ) ;
## }
2.4    Obten ̧c ̃ao de informa ̧c ̃oes sobre ficheiro
Exemplo com fun ̧c ̃ao para determina ̧c ̃ao de existˆencia e tamanho do ficheiro de descri ̧c ̃ao de evento
#i n c l u d e<s t a t . h>
i n t   C h e c k D e s c F i l e ( c h a r∗fname )
## {
s t r u c t   s t a t   f i l e s t a t ;
i n t   r e t
s t a t ;
r e t
s t a t=s t a t ( fname ,  & f i l e s t a t ) ;
i f ( r e ts t a t ==−1||f i l e s t a t . s ts i z e ==0)
r e t u r n ( 0 ) ;
## 12

r e t u r n ( f i l e s t a t . s ts i z e ) ;
## }
2.5    Listagem ordenada de conte ́udos de directorias
H ́a situa ̧c ̃oes que requerem a ordena ̧c ̃ao de informa ̧c ̃ao contida em directorias.  Para tal, o servidor ES no ‘tejo’ aproveita as funcionalidades
dofilesystemdo Linux no que respeita `a obten ̧c ̃ao de listagens de directorias com os conte ́udos ordenados.  Exemplifica-se a seguir uma
fun ̧c ̃ao hipot ́etica GetEventList().  As entradas relevantes da directoria s ̃ao carregadas uma a uma para a vari ́avel apontada por list.
#i n c l u d e<d i r e n t . h>
i n t   G e t E v e n t L i s t ( c h a r∗EID ,   EVENTLIST∗l i s t )
## {
s t r u c t   d i r e n t∗∗f i l e l i s t ;
i n t   n
e n t r i e s  , n oe v e n t s , ie n t =0;
c h a r   dirname [ 5 5 ] ;
s p r i n t f ( dirname , ” USERS/%s /CREATED/ ” , EID ) ;
n
e n t r i e s  =  s c a n d i r ( dirname ,  & f i l e l i s t  ,   0 ,   a l p h a s o r t ) ;
n oe v e n t s =0;
i f   ( ne n t r i e s<=  0 )
r e t u r n ( 0 ) ;
e l s e
## {
w h i l e   ( i
e n t<ne n t r i e s )
## {
i f ( f i l e l i s t [ i
e n t ]−>dname [ 0 ] ! = ’ . ’ )
## 13

## {
memcpy ( l i s t−>e v e n t
n o [ n oe v e n t s ] , f i l e l i s t [ ie n t ]−>dname , 3 ) ;
l i s t−>e v e n t
n o [ n oe v e n t s ] [ 3 ] = 0 ;
++n oe v e n t s ;
## }
f r e e ( f i l e l i s t [ i
e n t ] ) ;
++ie n t ;
## }
l i s t−>n o
e v e n t s=n oe v e n t s ;
f r e e ( f i l e l i s t  ) ;
## }
r e t u r n ( n o
e v e n t s ) ;
## }
Variantes da fun ̧c ̃ao GetEventList() podem ser usadas para seleccionar apenas a maior reserva, ou o evento
2.6    Processamento de tempos e datas
A execu ̧c ̃ao da fun ̧c ̃ao time(&fulltime),  retorna na vari ́avel designada fulltime que  ́e do tipotimet,  o n ́umero de segundos decorridos desde a data 1970-01-01
00:00:00 at ́e ao momento presente - tamb ́em conhecido porUnix time stamp.  Recomenda-se cuidado na utiliza ̧c ̃ao do tipotimet, nomeadamente na sua intera ̧c ̃ao
com inteiros.  Nalgumas situa ̧c ̃oes o tipotimetpode interagir com inteiros sem precau ̧c ̃oes especiais.  Os alunos devem verificar caso a caso e nas m ́aquinas que v ̃ao
usar para o desenvolvimento as precau ̧c ̃oes a tomar para o efeito.  O n ́umero de segundos decorridos desde 1970-01-01 00:00:00 at ́e ao momento em que o presente
projecto vai ser avaliado n ̃ao excede a capacidade de um inteiro de 32 bits (4 bytes) com sinal.
A convers ̃ao do n ́umero de segundos decorridos desde 1970-01-01 00:00:00 at ́e um dado momento para o formato DD-MM-YYYY HH:MM:SS (data de calend ́ario)
consegue-se usando a fun ̧c ̃ao gmtime() como se exemplifica a seguir:
#i n c l u d e<t i m e . h>
t i m e
t   f u l l t i m e ;
s t r u c t   tm∗c u r r e n t
t i m e ;
c h a r   t i m es t r [ 2 0 ] ;
## 14

t i m e (& f u l l t i m e ) ;   //   Get   c u r r e n t   t i m e   i n   s e c o n d s   s t a r t i n g   a t   1970−. . .
c u r r e n tt i m e  =  gmtime(& f u l l t i m e ) ;   //   C on v e rt   t i m e   t o  DD−MM−YYYY HH:MM: SS .   c u r r e n tt i m e   p o i n t s   t o   a   s t r u c t   o f   t y p e  tm
s p r i n t f ( t i m es t r ,”%2d−%02d−%04d  %02d:%02d:%02d ,
c u r r e n t
t i m e−>tmmday , c u r r e n tt i m e−>tmmon+1 , c u r r e n tt i m e−>t my e a r +1900 ,
c u r r e n tt i m e−>tmhour , c u r r e n tt i m e−>tmmin , c u r r e n tt i m e−>t ms e c ) ;
Para compara ̧c ̃ao de datas pode ser  ́util converter uma determinada data de calend ́ario naUnix time stamp.  Para tal preenche-se uma estrutura do tipostruct
tmcom os valores relevantes e usa-se a fun ̧c ̃ao mktime().  Como se exemplifica a seguir para a convers ̃ao da data de calend ́ario de um evento paraUnix time stamp:
#i n c l u d e<t i m e . h>
t i m et   c r e a t i o nu n i x t i m e ;
s t r u c t   tm   c r e a t i o nd a t e t i m e ;
c r e a t i o n
d a t e t i m e . tmmday=e v e n td a y ;
c r e a t i o nd a t e t i m e . tmmon=eventmonth−1;
c r e a t i o n
d a t e t i m e . t my e a r=e v e n ty e a r−1900;
c r e a t i o n
d a t e t i m e . tmhour=e v e n th o u r ;
c r e a t i o nd a t e t i m e . tmmin=e v e n tm i n ;
c r e a t i o nd a t e t i m e . t ms e c =0;
c r e a t i o n
u n i x t i m e=mktime(& c r e a t i o nd a t e t i m e ) ;
2.7    Extens ̃oes ao protocolo
A sec ̧c ̃ao do enunciado do projecto -7 Open Issues- sugere a possibilidade de extender o protocolo especificado.  A directoria RESERVATIONS sob cada evento,
acima descrita, n ̃ao tem interesse pr ́atico na actual especifica ̧c ̃ao do protocolo.  Contudo, a informa ̧c ̃ao nela contida permite recuperar rapidamente o detalhe das
reservas efectuadas para cada evento.
Os alunos podem extender o protocolo na vertente da comunica ̧c ̃ao TCP para recuperar essa informa ̧c ̃ao, adicionando comandos que permitam obtˆe-la com crit ́erios
de filtragem especificados nesses novos comandos.  Como por exemplo:  Todas as reservas efectuadas para um dado evento entre duas datas.  Ou todas as reservas
## 15

efectuadas para um dado evento por um utilizador.
## 16