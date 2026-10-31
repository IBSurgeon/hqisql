# Guia do usuário do hqisql

> Traduzido do guia em inglês, versão 1.0.0.

O hqisql é um console interativo para o Firebird. Ele é compilado a partir
do mesmo código-fonte do isql e aceita os mesmos comandos e as mesmas opções
de linha de comando. O hqisql adiciona um editor de linha baseado no replxx
(o fork do ClickHouse), realce de sintaxe, completamento com Tab e vários
comandos novos. O comportamento do próprio isql não muda.

## Recursos

- Realce de sintaxe de SQL e dos comandos do isql. Você pode mudar as cores
  com um arquivo de esquema de cores.
- Editor de várias linhas: Enter continua o comando até o terminador.
- Completamento com Tab e dicas: palavras-chave, e tabelas, colunas,
  procedures, functions, generators e packages do banco de dados conectado.
- Histórico de comandos: um comando de várias linhas é uma entrada do
  histórico.
- `PREPARE`, `EXEC SQL` e `DEALLOCATE`: executam um comando com valores
  diferentes.
- `IMPORT INTO` e `EXEC SQL ... FROM`: carregam dados de arquivos CSV, texto
  `COPY` do PostgreSQL, Apache Parquet e Avro.
- `SET PAGELENGTH`: define o número de linhas entre os cabeçalhos das
  colunas.
- `SET PREVIEW`: mostra as primeiras linhas de um resultado como uma tabela
  compacta.
- Os modos não interativos (`-i`, pipe, `-x`, `-a`, `INPUT`) usam o código
  do isql. Com os mesmos scripts (sem `PREPARE`, `EXEC SQL`, `DEALLOCATE` e
  `IMPORT`), o hqisql e o isql geram a mesma saída, byte a byte.
- Funciona no Windows 10 e mais novo (Windows Terminal, conhost) e no Linux.

## Uso

Os comandos e as opções de linha de comando são os mesmos do isql. O hqisql
adiciona estes:

- `-noedit` ou a variável de ambiente `HQISQL_NOEDIT=1`: sem editor; o
  hqisql funciona como o isql. O hqisql não altera o console e não usa o
  arquivo de histórico.
- `NO_COLOR`: sem realce. `HQISQL_COLORS=<file>`: o seu próprio esquema de
  cores (veja "Esquema de cores").
- `-z` imprime `hqisql 1.0.0` e depois a linha de versão do isql.

Teclas:

- Tab: completamento. Se há um candidato, ele é inserido por inteiro. Se há
  vários candidatos, o hqisql insere o prefixo comum e mostra a lista; Ctrl-N
  e Ctrl-P selecionam um candidato.
- Seta para a direita ou End no fim da linha: aceita a dica em cinza.
- Ctrl-Up e Ctrl-Down: percorrem as dicas.
- Ctrl-R: pesquisa no histórico.
- Ctrl-C: limpa a linha ou o comando que você começou.
- Ctrl-D em uma linha vazia: sai (EOF).

## Editor de linha

Entrada de várias linhas:

- Enter continua o comando até o terminador. O prompt muda para `CON>`.
- `SET TERM` funciona. O realce usa o novo terminador.

Colar da área de transferência:

- Windows: o console não tem bracketed paste. O hqisql pega as linhas que
  já estão no buffer de entrada do console e executa todas de uma vez, como
  o isql faz. Cada linha é mostrada com o prompt `CON>`. Colar um comando de
  2000 linhas leva cerca de 0,2 s (isql: cerca de 0,7 s).
- Linux e macOS: o bracketed paste está ativo. O texto colado entra no
  editor como um todo, e o Enter executa o texto. Se o texto colado tem
  vários comandos, o hqisql executa um após o outro. Colar um comando de
  2000 linhas leva cerca de 0,3 s no Linux. Sem bracketed paste, as linhas
  que chegam em um bloco são executadas como no isql.

Ctrl-C:

- no prompt `SQL>`, o Ctrl-C limpa a linha;
- no prompt `CON>` e dentro de uma string aberta, o Ctrl-C limpa o comando
  inteiro;
- enquanto uma consulta é executada, o Ctrl-C cancela a consulta, como no
  isql.

Em todos esses casos, a sessão não termina.

Histórico:

- O hqisql guarda o histórico por comando. Um comando de várias linhas é uma
  entrada. A entrada guarda o terminador que você digitou com o comando.
- O arquivo de histórico é `%LOCALAPPDATA%\hqisql\history.txt` no Windows e
  `~/.hqisql_history` em POSIX (permissões de acesso 0600).
- O replxx grava as quebras de linha dentro de uma entrada como o byte 0x17.
  O `cat` não mostra esse byte.
- O hqisql salva o arquivo após cada comando. Se o processo for encerrado à
  força, o histórico não se perde. Depois de colar texto, o hqisql salva o
  arquivo antes do próximo prompt.
- Comandos com a palavra `PASSWORD` (`CONNECT ... PASSWORD`,
  `CREATE USER ...`) não entram no histórico.

## Realce de sintaxe

As cores internas:

- palavras reservadas: azul claro;
- outras palavras-chave: azul, para que `NAME`, `TYPE`, `VALUE` como nomes
  de colunas não chamem atenção;
- comandos do isql: amarelo. Isso inclui a palavra do comando (`SHOW`,
  `HELP`, `INPUT`/`IN`, `OUTPUT`/`OUT`, `EDIT`, `SHELL`, `EXIT`, ...), a
  opção do `SET` (`STATS`, `AUTODDL`, `TERM`, `BLOB`, ...), o objeto do
  `SHOW` (`TABLES`, `DB`, `VERSION`, ...) e o parâmetro depois deles
  (`ON`/`OFF`, `ALL`, `DIALECT`). As listas de palavras e as abreviações são
  as mesmas que o isql aceita (`SET STAT ON`, `SHOW TABLE`);
- `SET` sem uma opção do isql (`SET TRANSACTION`, `SET GENERATOR`,
  `SET ROLE`, `SET TIME ZONE`, `SET BIND`) é SQL e recebe as cores normais de
  SQL;
- strings: verde (texto em cirílico também);
- números: magenta;
- comentários: cinza;
- o terminador: vermelho;
- parâmetros `:` e `?`: ciano.

As linhas de continuação `CON>` são coloridas levando em conta as linhas
anteriores do comando. Por exemplo, o hqisql sabe que uma linha está dentro
de uma string ou de um comentário aberto.

Você pode mudar as cores com um arquivo de esquema de cores (veja "Esquema
de cores").

## Completamento com Tab

- Completamento com Tab e dicas enquanto você digita: palavras-chave, e
  tabelas, colunas, procedures, functions, generators e packages do banco de
  dados conectado. O hqisql carrega os nomes em segundo plano.
- **Nomes com maiúsculas/minúsculas e aspas.** O hqisql guarda os nomes como
  eles são: `from ord` + Tab gera `"Orders"`, `o."I` + Tab gera `"Id"`. O
  hqisql coloca um nome entre aspas quando ele não pode ser lido sem elas
  (letras minúsculas, espaços, uma palavra reservada). Sem aspas, a
  comparação ignora maiúsculas e minúsculas. Entre aspas, a comparação é
  exata.
- **Atualização após DDL.** Depois de `CREATE`, `ALTER`, `DROP`, `RECREATE`,
  `DECLARE` e `COMMIT`, o hqisql lê os metadados de novo em segundo plano
  antes do próximo prompt. Exemplo: depois de `create table "Abc"`, `"Ab` +
  Tab gera `"Abc"` sem uma nova conexão.
- **Completamento qualificado.** Depois de `Table.`, o hqisql mostra apenas
  as colunas dessa tabela. Depois de `Package.`, ele mostra as procedures e
  functions do package. O hqisql encontra `Alias.` no texto da consulta
  (`FROM`, `JOIN`, `UPDATE`, listas com vírgulas, `AS` opcional, um alias
  entre aspas). O contexto inclui todas as linhas de continuação `CON>` e o
  texto depois do cursor até o fim do comando. Assim, `SELECT E.` + Tab
  funciona também quando `FROM EMPLOYEE E` já está escrito à direita do
  cursor. Outros comandos na mesma linha não são usados.
- **Posição de tabela.** Depois de `FROM`, `JOIN` e `UPDATE`, o hqisql
  mostra apenas tabelas e views. As tabelas de sistema (`RDB$`, `MON$`,
  `SEC$`, `TMP$`) ficam ocultas.

## Conjuntos de caracteres

O hqisql lê e imprime dados na code page do console, como o isql. O replxx
muda o console do Windows para UTF-8 e modo VT apenas enquanto você edita
uma linha. Ele restaura os valores antigos antes de o isql continuar. A
linha digitada é convertida para a code page de entrada do console. Assim,
as regras são as mesmas do isql: `-ch` deve corresponder à code page do
console (`chcp 1251` + `-ch WIN1251`, `chcp 65001` + `-ch UTF8`).

Um banco de dados WIN1251 no console com code page 1251, e `-ch UTF8` no
console com code page 65001, geram os mesmos bytes que o isql. O mesmo vale
no Linux (UTF-8, `-ch UTF8`).

## PREPARE / EXEC SQL / DEALLOCATE

Estes comandos existem apenas no hqisql. Eles funcionam no modo interativo e
em scripts (`-i`, `INPUT`, pipe).

```
PREPARE name AS <statement with ? >;
EXEC SQL name USING (v1, v2, ...);
EXEC SQL name USING BATCH (v1, ...), (v1, ...), ...;
DEALLOCATE name;
```

**Isto é uma substituição de texto no cliente, não um prepare no
servidor.** O `EXEC SQL` substitui cada `?` (fora de strings, nomes entre
aspas e comentários) pelo valor como ele está escrito. O resultado é um
comando novo. O servidor prepara e executa esse comando de novo. Portanto:

- um valor é qualquer expressão SQL (`1`, `'text'`, `current_date`,
  `(2 + 3) * 4`); coloque strings entre aspas;
- `BATCH` são N comandos separados, não `IBatch`;
- o nome vale até o fim da sessão. Um novo `PREPARE` com o mesmo nome
  substitui o comando.

Como o hqisql lê os valores:

- strings `'...'` (com `''`), Q-strings `q'{...}'`, nomes `"..."` e
  parênteses aninhados dentro de um valor ficam como estão;
- o hqisql remove os comentários dos valores (um comentário de linha
  comentaria o resto da linha do comando);
- depois da lista, apenas espaços e comentários são permitidos. Texto a
  mais, um valor vazio ou um `)` faltando é um erro;
- `()` é a lista para um comando sem `?`.

Como o hqisql executa o comando:

- um `EXEC SQL` simples reescreve o comando e o passa para o processamento
  normal do isql (comandos do isql, saída e transações funcionam como na
  entrada direta);
- `BATCH` primeiro verifica todas as tuplas (o número de valores). Depois
  executa cada comando da mesma forma: primeiro os comandos do isql (`SHOW`,
  `SET` ...), depois o SQL. Ele para no primeiro erro;
- um erro do próprio comando (`EXEC SQL: ...`) define o código de saída de
  erro, respeita o `SET BAIL` e, em um script, imprime "After line N in file
  F", como o isql faz após um erro de comando.

## Carga de arquivos: EXEC SQL ... FROM e IMPORT INTO

Estes comandos existem apenas no hqisql. Eles funcionam no modo interativo e
em scripts. A sintaxe `FROM CSV` vem de
[FirebirdSQL/firebird#9046](https://github.com/FirebirdSQL/firebird/issues/9046).

```
PREPARE ins AS INSERT INTO ORDERS (ID, CUSTOMER, AMOUNT) VALUES (?, UPPER(?), ?);
EXEC SQL ins FROM CSV 'orders.csv' OPTIONS (HEADER, ERRORS = 10);

IMPORT INTO EMPLOYEE FROM CSV 'emp.csv' OPTIONS (HEADER, DELIMITER = ';');
IMPORT INTO "Orders" ("Id", AMOUNT) FROM CSV 'C:\data\o.csv' OPTIONS (ENCODING = WIN1251, DECIMAL = ',');
```

- **EXEC SQL name FROM CSV**: o hqisql executa o comando do `PREPARE` para
  cada registro do arquivo. O campo N é o valor do N-ésimo `?`. Comandos
  permitidos: `INSERT`, `UPDATE`, `DELETE`, `MERGE`, `EXECUTE PROCEDURE`,
  `EXECUTE BLOCK`. `SELECT` é um erro.
- **IMPORT INTO table**: o hqisql monta o `INSERT` sozinho. As colunas vêm
  da lista no comando, ou do cabeçalho do arquivo (`HEADER`), ou são todas
  as colunas da tabela em ordem, exceto colunas calculadas e colunas
  `GENERATED ALWAYS AS IDENTITY`. Um nome do cabeçalho: um identificador
  normal é convertido para maiúsculas; qualquer outro nome é colocado entre
  aspas como está. Uma view também funciona.
- Um nome de arquivo relativo é relativo ao diretório do script (`-i`,
  `INPUT`). No console, ele é relativo ao diretório atual.

Como funciona:

- o servidor prepara o comando **uma vez**, na transação DML do isql (com
  `USING`, os valores são colocados no texto);
- cada campo vai como `VARCHAR` no conjunto de caracteres da conexão (ou no
  `ENCODING`; após um BOM UTF-8, em UTF8). O servidor converte o valor para
  o tipo do parâmetro (data, número, BOOLEAN, BLOB), como para um literal;
- os registros vão em batches `IBatch` (`BATCH`, buffer de 16 MB). Cada
  batch é executado sob `SAVEPOINT HQ$IMPORT`. Se um batch falha, o hqisql
  faz o rollback dele até o savepoint e executa os registros dele de novo,
  um por um. Assim, o erro recebe o número exato da linha no arquivo;
- um servidor sem `IBatch` (Firebird 3, protocolo < 16): um registro por
  vez, como com `BATCH = 0`. Testado com servidores Firebird 3.0 via TCP: o
  mesmo script de teste usado para o Firebird 5 gera o mesmo resultado.

### Opções

| Opção | Padrão | Significado |
|---|---|---|
| `DELIMITER` | `','` | delimitador de campo; `'\t'` é um tab |
| `QUOTE` | `'"'` | caractere de aspas; `""` dentro de um campo é a própria aspa |
| `HEADER` | `FALSE` | o primeiro registro tem os nomes das colunas; `HEADER` sem valor = `TRUE` |
| `SKIP` | `0` | pula n registros depois do cabeçalho |
| `NULL` | `''` | um campo sem aspas com este texto é NULL; por padrão, um campo vazio é NULL e `""` é uma string vazia |
| `ENCODING` | conjunto de caracteres da conexão | conjunto de caracteres do arquivo (`WIN1251`, `UTF8`, ...) |
| `DECIMAL` | `'.'` | `','` significa vírgula nos números (`1,5`) |
| `BATCH` | `1000` | registros em um batch; `0` significa sem `IBatch`, um por vez |
| `COMMIT` | `0` | commit a cada n registros e no fim; `0` significa que o usuário termina a transação |
| `ERRORS` | `0` | quantos registros com erro podem ser pulados |

### Erros

- Sem `ERRORS`, a carga para no primeiro registro com erro:
  `file, line N:`, o erro do servidor, `After line M in file script.sql`.
  Os registros anteriores ficam na transação (como com `SET BULK_INSERT`).
  O código de saída é de erro; o `SET BAIL` é respeitado.
- Com `ERRORS = n`: `file, line N: rejected: message`. O hqisql grava o
  registro em `<file>.bad` (primeiro a linha de cabeçalho, se houver). No
  (n+1)-ésimo registro com erro, a carga para.
- Um registro com número errado de campos, um campo maior que a coluna ou
  uma aspa não fechada gera um erro com o número da linha. O hqisql não
  envia esse registro para o servidor.
- O Ctrl-C cancela a carga (`cancelOperation`). Os registros do batch atual
  sofrem rollback.

### Resumo e velocidade

No fim, o hqisql imprime `N rows imported, K rejected (path .bad), T s`
(`executed` para `EXEC SQL`). Com `-q`, o hqisql não imprime o resumo; se
há registros rejeitados, ele imprime o resumo no stderr. No console, o
hqisql imprime um contador de registros uma vez por segundo.

Velocidade: 1 milhão de registros em uma tabela
`(INT, VARCHAR(30), NUMERIC(12,2), DATE)`, Firebird 5.0, tempo de execução
do hqisql sem a criação do banco de dados:

| Método | Windows 11, embedded | Windows 11, TCP localhost (SuperServer) |
|---|---|---|
| `IMPORT INTO` (BATCH 1000) | 1,3 s | 2,2 s |
| `IMPORT INTO`, `BATCH = 0` | 2,5 s | 45,9 s |
| `SET BULK_INSERT` | 3,4 s | 70,9 s |
| `EXEC SQL ... USING BATCH` (100.000 registros) | 2,6 s | 21,8 s |

No Linux (Ubuntu 22.04, embedded): `IMPORT INTO` leva 11,1 s e
`SET BULK_INSERT` leva 22,3 s.

O `EXEC SQL ... USING BATCH` prepara cada comando de novo. Para 1 milhão de
registros, ele leva cerca de 26 s e 218 s. Na rede, o `IBatch` dá o maior
ganho, porque não há uma ida e volta ao servidor para cada registro.

### Formatos COPY, PARQUET e AVRO

A sintaxe é a mesma. Só a palavra do formato muda:

```
IMPORT INTO T FROM COPY 'dump.txt';
IMPORT INTO T (ID, NAME, AMOUNT) FROM PARQUET 'orders.parquet' OPTIONS (BATCH = 5000);
EXEC SQL ins FROM AVRO 'events.avro' OPTIONS (ERRORS = 10);
```

| Formato | O que é | Opções |
|---|---|---|
| `CSV` | RFC 4180 (veja acima) | todas |
| `COPY` | formato texto do PostgreSQL de `COPY ... TO` / `\copy` / dados do pg_dump | todas, exceto `QUOTE`; o `DELIMITER` padrão é um tab, o `NULL` padrão é `\N` |
| `PARQUET` | Apache Parquet | `SKIP`, `BATCH`, `COMMIT`, `ERRORS` |
| `AVRO` | Avro object container (`.avro`) | `SKIP`, `BATCH`, `COMMIT`, `ERRORS` |

Qualquer outra opção para um formato é um erro de parse (`option HEADER is not for
PARQUET files`).

**COPY**: uma linha é um registro, `\N` é NULL. Sequências de escape:
`\t \n \r \b \f \v`, `\\`, octal `\101`, `\x41`. A linha `\.` é o fim dos
dados. O hqisql detecta o formato binário `PGCOPY` e o rejeita. `HEADER`,
`ENCODING` e `DECIMAL` funcionam como no CSV. O hqisql converte os booleanos
`t` / `f` e o offset `+03` em `timestamptz` para uma forma que o Firebird
entende.

**PARQUET e AVRO**: o arquivo tem um schema, então as colunas são
associadas **pelo nome**:

- `IMPORT INTO t (a, b)`: as colunas `a`, `b` do arquivo vão para as colunas
  `A`, `B` da tabela. Sem lista: todas as colunas do arquivo; o nome de uma
  coluna do arquivo é o nome da coluna da tabela (`id` → `ID`). Um nome sem
  aspas é encontrado sem diferenciar maiúsculas e minúsculas. Se não existe
  essa coluna, o erro mostra a lista das colunas do arquivo;
- `EXEC SQL name`: todas as colunas do arquivo, em ordem. A quantidade delas
  deve ser igual à quantidade de `?`;
- Parquet: uma coluna de um grupo aninhado tem o nome `group.column`. Listas
  e maps (colunas repetidas) não podem ser carregados; selecionar um deles é
  um erro;
- Avro: os campos do record de nível superior. Uma union com null é uma
  coluna com NULL. record, array e map viram texto JSON. Se o schema não é
  um record, o arquivo tem uma coluna `VALUE`.

Os valores vão como texto, como no CSV, com estas exceções:

- bytes (Parquet `BYTE_ARRAY` sem tipo lógico, `FIXED_LEN_BYTE_ARRAY`;
  Avro `bytes`, `fixed`) vão como `OCTETS` para `VARBINARY` /
  `CHAR(n) CHARACTER SET OCTETS` / `BLOB` sem conversão;
- um UUID vai como 16 bytes para uma coluna `OCTETS`; caso contrário, vai
  como o texto `12345678-9abc-def0-1234-56789abcdef0`;
- um horário em UTC (Parquet `TIMESTAMP(isAdjustedToUTC)`, não INT96; Avro
  `timestamp-*`) recebe ` +00:00` para uma coluna `WITH TIME ZONE`. Para uma
  coluna `TIMESTAMP`, o valor é UTC sem fuso horário;
- a fração de segundo é cortada para 4 dígitos (a precisão do Firebird).
  `DECIMAL` é um número exato; `FLOAT` / `DOUBLE` usam a forma exata mais
  curta.

Parquet: páginas v1 e v2, dicionários, os encodings PLAIN, RLE, BIT_PACKED,
DELTA_BINARY_PACKED, DELTA_LENGTH_BYTE_ARRAY, DELTA_BYTE_ARRAY,
BYTE_STREAM_SPLIT; as compressões SNAPPY, GZIP, LZ4, LZ4_RAW, ZSTD (BROTLI
e LZO geram erro). Avro: os codecs null, deflate, snappy, zstandard (bzip2 e
xz geram erro).

O hqisql tem o seu próprio leitor para esses formatos e não precisa de
bibliotecas extras, exceto para dados comprimidos. Ele carrega zlib e zstd
no primeiro bloco comprimido: `zlib1.dll` (o diretório do hqisql, depois o
`PATH`) / `libz.so.1`, e `libzstd.dll` ou `zstd.dll` / `libzstd.so.1`. Se a
biblioteca não existe, o erro é `the zstd compression needs ...`. O hqisql
lê o arquivo por páginas (Parquet) e blocos (Avro). Ele não carrega o
arquivo inteiro na memória.

Nas mensagens e no `.bad`, o número do registro é `file, row N`. No `.bad`,
o registro é uma linha CSV; a primeira linha tem os nomes das colunas.

Testado com o Firebird 5 (embedded e TCP) e com servidores Firebird 3.0 via
TCP (um registro por vez): Parquet (snappy, zstd v2, gzip), Avro (null,
deflate), COPY. 200.000 linhas Parquet (zstd) são carregadas em cerca de
1 s no Firebird 5 embedded, e em cerca de 40 s no Firebird 3.0 via TCP.

### Suporte no editor e HELP

O editor colore `PREPARE`, `EXEC SQL`, `DEALLOCATE`, `IMPORT INTO`,
`FROM CSV` (e `COPY`, `PARQUET`, `AVRO`), `OPTIONS` e os nomes das opções
com a cor dos comandos do isql. Completamento com Tab: depois de
`IMPORT INTO`, tabelas e views; depois de `FROM`, os formatos; dentro de
`OPTIONS (`, os nomes das opções; depois de `EXEC SQL` e `DEALLOCATE`, os
nomes do `PREPARE`. O `HELP` mostra os comandos e as opções do hqisql.

## SET PAGELENGTH

Este comando existe apenas no hqisql. No isql, você só pode definir o
número de linhas depois do qual os cabeçalhos das colunas e `====` são
impressos de novo com a opção de linha de comando `-pag <n>`.

```
SET PAGELENGTH <n>;   -- header every n rows (short form: SET PAG n)
SET PAGELENGTH 0;     -- header once per query
SET PAGELENGTH;       -- default, 20
```

Um valor negativo ou um valor que não é número é um erro. O `SET` mostra o
valor atual (`Page length:`). O comando aparece no `HELP SET`. Ao contrário
de `-pag 0`, o `SET PAGELENGTH 0` não desliga o `SET HEADING`.

## SET PREVIEW

Este comando existe apenas no hqisql. Ele liga o modo de pré-visualização.
O hqisql imprime o resultado da consulta como uma tabela compacta, sem
quebrar os cabeçalhos e as linhas:

```
SET PREVIEW ON;       -- switch on (SET PREVIEW without an argument toggles)
SET PREVIEW 50;       -- switch on and show the first 50 rows (default 20)
SET PREVIEW OFF;
```

O que ele faz:

- o hqisql busca apenas as primeiras N linhas. O fetch para, e o servidor
  não lê mais nada. Se o limite foi atingido, o hqisql imprime
  `-- preview: first 20 rows` depois da tabela;
- a largura de cada coluna é a largura do maior valor ou cabeçalho, não o
  tamanho declarado. Os números ficam alinhados à direita; `<null>` é
  mostrado como no isql;
- as colunas que não cabem na largura da tela são removidas à direita. A
  última coluna mostrada pode ser cortada (para no mínimo 4 caracteres). A
  lista das colunas removidas fica na mesma linha:
  `-- preview: ... 2 more columns: C3 C4`. O hqisql pega a largura da tela
  do console. Se a saída não vai para um terminal (`OUTPUT file`, pipe), a
  largura não tem limite, mas as colunas continuam estreitas;
- `SET LIST ON` desliga o modo para a consulta. `SET HEADING OFF` remove as
  linhas de cabeçalho. O hqisql imprime apenas o identificador de um campo
  BLOB (ele não imprime o conteúdo do BLOB no modo de pré-visualização).
  `SET COUNT ON` conta as linhas buscadas.

As larguras são contadas em caracteres (para uma conexão UTF8, em code
points; caso contrário, em bytes). O `SET` mostra o estado atual
(`Preview: ON, 20 rows`).

## Esquema de cores

Um arquivo de texto opcional define as cores do realce. O hqisql lê esse
arquivo uma vez na inicialização (apenas quando o editor está ativo e
`NO_COLOR` não está definida):

- `%LOCALAPPDATA%\hqisql\colors.txt` no Windows, `~/.hqisql_colors` em POSIX
  (ao lado do arquivo de histórico);
- a variável `HQISQL_COLORS=<path>` indica outro arquivo. Se esse arquivo
  não existe, o hqisql imprime um aviso no stderr e usa as cores internas.

Sem arquivo, o hqisql usa as cores internas. O arquivo de exemplo
[hqisql-colors.txt](../hqisql-colors.txt) tem as cores internas e descreve
o formato. Você pode copiar e alterar esse arquivo. O formato é uma linha
para cada elemento; `#` inicia um comentário:

```
# <element> = <color> [bold] [underline] [on <color>]
command    = yellow            # isql commands and their options: SET STATS ON, SHOW TABLE
reserved   = brightblue bold   # reserved words
keyword    = blue              # other keywords (NAME, TYPE)
string     = rgb(0,5,0)        # 'string'
quoted     = brightmagenta     # "quoted name"
number     = 213               # terminal palette index 0..255
comment    = gray(10)          # gray level 0..23
param      = default underline # :name and ?
terminator = brightred
hint       = gray              # the hint after the cursor
completion = brightblue        # keywords in the Tab completion list
```

Cores:

- `default`: a cor do texto do terminal. `default` sem atributos significa
  "não colorir";
- os 16 nomes do replxx: `black red green brown blue magenta cyan lightgray
  gray brightred brightgreen yellow brightblue brightmagenta brightcyan
  white`;
- `rgb(r,g,b)` com componentes de 0..5 (o cubo de 216 cores);
- `gray(n)` com n de 0 a 23;
- um número de 0 a 255 (o índice da paleta do terminal).

Os nomes não diferenciam maiúsculas e minúsculas. O separador é `=` ou `:`.
Um elemento que não está no arquivo mantém a sua cor interna. O hqisql pula
uma linha que ele não entende e imprime uma mensagem, por exemplo
`hqisql: color scheme <file>: line N: unknown color 'pink'`. As cores
16..255 precisam de um terminal com 256 cores (Windows Terminal, conhost no
Windows 10 e mais novo, xterm-256color).

## Compilação

O hqisql não é compilado sozinho. Você o instala em uma árvore de
código-fonte do Firebird 5.0 e o compila junto com o Firebird. O hqisql é
testado com o Firebird `v5.0.4` e `v5.0-release` (o futuro 5.0.5).

Você precisa de:

- git. O `install.sh` usa `git apply`. A árvore do Firebird não precisa ser
  uma working tree do git.
- Windows: Git for Windows (o `install.bat` executa o `install.sh` com o
  bash dele) e Visual Studio 2022. Você também pode usar o Visual Studio
  2026 com o Firebird `v5.0-release`; o Firebird 5.0.4 não conhece o Visual
  Studio 2026.

### Linux

```
git clone -b v5.0.4 https://github.com/FirebirdSQL/firebird
git clone https://github.com/IBSurgeon/hqisql
hqisql/install.sh firebird && cd firebird && ./autogen.sh && make -j8 && make run_tests
```

O resultado é `gen/Release/firebird/bin/hqisql`. `make tests` e
`make run_tests` compilam e executam os testes unitários `hqisql_test`
junto com os testes do Firebird.

### Windows

```
hqisql\install.bat C:\src\firebird
```

Depois, em `builds\win32`, execute `make_icu.bat`, `make_boot.bat` e
`make_all.bat` (ou
`run_all.bat`). O `make_all.bat` compila o hqisql (a solution `HQisql.sln`),
executa o `hqisql_test` e copia `hqisql.exe` e `replxx-LICENSE.md` para
`output_<platform>`.

### Opções de instalação

O `install.sh` e o `install.bat` têm estas opções:

- `--check`: apenas verifica se os patches podem ser aplicados. Nada muda.
- `--remove`: reverte os patches e apaga os arquivos.
- `--no-hook`: não altera os arquivos de build do Firebird. Nesse caso,
  compile o hqisql depois do Firebird com
  `make -C gen -f ../builds/posix/Makefile.hqisql` (Linux) ou
  `builds\win32\make_hqisql.bat` (Windows).

Se você executar a instalação de novo com a mesma versão, ela não faz nada.
Uma versão nova substitui a antiga. O registro da instalação fica em
`src/hqisql/.install`.

O que a instalação altera na árvore do Firebird:

- copia `src/hqisql`, `extern/replxx` e os arquivos de build do hqisql;
- aplica patch em `src/isql/isql.epp`. Todas as alterações ficam sob
  `#ifdef HQISQL`, então o isql normal não muda;
- a menos que você use `--no-hook`, aplica patch em
  `builds/posix/Makefile.in` e `builds/win32/make_all.bat`.

### Execução

- O hqisql precisa do fbclient do Firebird 5. O servidor pode ser Firebird
  3.0 ou mais novo.
- Arquivos Parquet e Avro comprimidos com zstd precisam da libzstd. Linux: o
  pacote do sistema (por exemplo `libzstd1`). Windows: `libzstd.dll` ao lado
  de `hqisql.exe`; o arquivo de release já inclui essa biblioteca.
- A zlib vem com o Firebird.
- `hqisql -z` imprime `hqisql 1.0.0` e depois a linha de versão do isql.

## Testes de integração

```
python3 tests/integration/run.py <firebird-dir>
```

O script executa os testes de `IMPORT` e `EXEC SQL` em um banco de dados
embedded e compara a saída com `tests/integration/expected/*.out`. Ele
precisa do pyarrow para gerar os dados Parquet e Avro.

## Limitações conhecidas

- O macOS não é compilado nem testado.
- Carga de arquivos: BLOBs maiores que 32 KB, `FROM STDIN`, exportação para
  CSV e criação de uma tabela a partir do cabeçalho do arquivo não são
  suportados.
- Não há editor externo (Alt+E), nem realce da posição do erro vinda do
  servidor, nem modo vi.

## Como funciona

Esta seção é para quem contribui com o projeto.

O hqisql é compilado a partir dos fontes do isql como um segundo target. O
build compila de novo o `isql.epp` e os outros fontes do isql com
`-DHQISQL` e adiciona os arquivos de `src/hqisql`. O `isql.epp` tem apenas
hooks finos sob `#ifdef HQISQL`. A interface dos hooks é
`src/hqisql/HqIsql.h`; é o único header do hqisql que o `isql.epp` inclui.
Sem `HQISQL`, o `isql.cpp` pré-processado é igual ao do Firebird, então o
isql não muda.

A entrada se divide em um único lugar: `readNextInputLine()` no `isql.epp`.
O hook fica antes do branch `HAVE_EDITLINE_H`. Se `Hq::readLine()` retorna
false (`-noedit`, não é um terminal, `-i`), o código normal do isql é
executado, incluindo o editline. No hqisql interativo, o replxx lê a linha.
O hqisql divide uma entrada de várias linhas em linhas, e o leitor de
comandos existente do isql (`get_statement`) funciona sem alterações. ECHO,
os números de linha dos erros, `INPUT`, `SET TERM` e o arquivo temporário
funcionam como no isql.

Os módulos em `src/hqisql`:

- `HqLineReader.cpp`: o wrapper do replxx (realce, completamento, dicas,
  histórico, Ctrl-C, colar) e o código dos hooks;
- `HqLexer`, `HqKeywords`, `HqIsqlCommands`, `HqColors`: realce e o esquema
  de cores;
- `HqMetadata`: carrega os nomes do banco de dados para o completamento em
  uma thread de segundo plano, na sua própria transação read-only;
- `HqStatements`, `HqParams`: `PREPARE`, `EXEC SQL`, `DEALLOCATE`;
- `HqImportCommand`, `HqImport`, `HqRows`, `HqCsv`, `HqCopy`, `HqParquet`,
  `HqAvro`, `HqCodec`: carga de arquivos;
- `HqTable`, `HqPreview`: `SET PREVIEW`;
- `tests/`: os testes unitários Boost.Test (`hqisql_test`).

O replxx fica em `extern/replxx`. Ele é uma cópia do ClickHouse/replxx; as
alterações locais estão listadas em `extern/replxx/REVISION`.
