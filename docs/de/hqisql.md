# hqisql Benutzerhandbuch

> Übersetzt aus dem englischen Handbuch, Version 1.0.0.

hqisql ist eine interaktive Konsole für Firebird. Sie wird aus demselben
Quellcode wie isql gebaut und akzeptiert dieselben Befehle und
Kommandozeilenoptionen. hqisql ergänzt einen Zeileneditor auf Basis von replxx
(dem ClickHouse-Fork), Syntaxhervorhebung, Tab-Vervollständigung und einige
neue Befehle. Das Verhalten von isql selbst ändert sich nicht.

## Funktionen

- Syntaxhervorhebung von SQL und isql-Befehlen. Sie können die Farben mit
  einer Farbschema-Datei ändern.
- Mehrzeiliger Editor: Enter setzt die Anweisung bis zum Terminator fort.
- Tab-Vervollständigung und Hinweise: Schlüsselwörter sowie Tabellen, Spalten,
  Prozeduren, Funktionen, Generatoren und Packages der verbundenen Datenbank.
- Verlauf der Anweisungen: Eine mehrzeilige Anweisung ist ein Eintrag im
  Verlauf.
- `PREPARE`, `EXEC SQL` und `DEALLOCATE`: eine Anweisung mit verschiedenen
  Werten ausführen.
- `IMPORT INTO` und `EXEC SQL ... FROM`: Daten aus CSV-Dateien,
  PostgreSQL-`COPY`-Text, Apache-Parquet- und Avro-Dateien laden.
- `SET PAGELENGTH`: die Anzahl der Zeilen zwischen den Spaltenköpfen festlegen.
- `SET PREVIEW`: die ersten Zeilen eines Ergebnisses als kompakte Tabelle
  anzeigen.
- Nicht-interaktive Modi (`-i`, Pipe, `-x`, `-a`, `INPUT`) verwenden den
  isql-Code. Bei denselben Skripten (ohne `PREPARE`, `EXEC SQL`, `DEALLOCATE`
  und `IMPORT`) liefern hqisql und isql dieselbe Ausgabe, Byte für Byte.
- Läuft unter Windows 10 und neuer (Windows Terminal, conhost) und unter
  Linux.

## Verwendung

Die Befehle und die Kommandozeilenoptionen sind dieselben wie in isql. hqisql
ergänzt diese:

- `-noedit` oder die Umgebungsvariable `HQISQL_NOEDIT=1`: kein Editor; hqisql
  arbeitet wie isql. hqisql ändert die Konsole nicht und verwendet keine
  Verlaufsdatei.
- `NO_COLOR`: keine Hervorhebung. `HQISQL_COLORS=<file>`: Ihr eigenes
  Farbschema (siehe "Farbschema").
- `-z` gibt `hqisql 1.0.0` aus, danach die Versionszeile von isql.

Tasten:

- Tab: Vervollständigung. Ein einzelner Kandidat wird vollständig eingefügt.
  Bei mehreren Kandidaten fügt hqisql den gemeinsamen Präfix ein und zeigt die
  Liste; mit Ctrl-N und Ctrl-P wählen Sie einen Kandidaten.
- Right oder End am Zeilenende: den grauen Hinweis übernehmen.
- Ctrl-Up und Ctrl-Down: durch die Hinweise blättern.
- Ctrl-R: im Verlauf suchen.
- Ctrl-C: die Zeile oder die begonnene Anweisung löschen.
- Ctrl-D in einer leeren Zeile: beenden (EOF).

## Zeileneditor

Mehrzeilige Eingabe:

- Enter setzt die Anweisung bis zum Terminator fort. Der Prompt wechselt zu
  `CON>`.
- `SET TERM` funktioniert. Die Hervorhebung verwendet den neuen Terminator.

Einfügen aus der Zwischenablage:

- Windows: Die Konsole hat kein Bracketed Paste. hqisql nimmt die Zeilen, die
  bereits im Eingabepuffer der Konsole stehen, und führt sie sofort aus, wie
  isql. Jede Zeile wird mit dem Prompt `CON>` angezeigt. Das Einfügen einer
  Anweisung mit 2000 Zeilen dauert etwa 0,2 s (isql: etwa 0,7 s).
- Linux und macOS: Bracketed Paste ist aktiv. Der eingefügte Text kommt als
  Ganzes in den Editor, und Enter führt ihn aus. Enthält der eingefügte Text
  mehrere Anweisungen, führt hqisql sie nacheinander aus. Das Einfügen einer
  Anweisung mit 2000 Zeilen dauert unter Linux etwa 0,3 s. Ohne Bracketed
  Paste werden die Zeilen, die in einem Block ankommen, wie in isql
  ausgeführt.

Ctrl-C:

- am Prompt `SQL>` löscht Ctrl-C die Zeile;
- am Prompt `CON>` und innerhalb eines offenen Strings löscht Ctrl-C die ganze
  Anweisung;
- während eine Abfrage läuft, bricht Ctrl-C die Abfrage ab, wie in isql.

In keinem dieser Fälle endet die Sitzung.

Verlauf:

- hqisql speichert den Verlauf pro Anweisung. Eine mehrzeilige Anweisung ist
  ein Eintrag. Der Eintrag enthält den Terminator, den Sie mit der Anweisung
  eingegeben haben.
- Die Verlaufsdatei ist `%LOCALAPPDATA%\hqisql\history.txt` unter Windows und
  `~/.hqisql_history` unter POSIX (Zugriffsrechte 0600).
- replxx speichert die Zeilenumbrüche innerhalb eines Eintrags als Byte 0x17.
  `cat` zeigt dieses Byte nicht an.
- hqisql speichert die Datei nach jeder Anweisung. Wird der Prozess beendet
  (kill), geht der Verlauf nicht verloren. Nach einem Einfügen speichert
  hqisql die Datei vor dem nächsten Prompt.
- Anweisungen mit dem Wort `PASSWORD` (`CONNECT ... PASSWORD`,
  `CREATE USER ...`) kommen nicht in den Verlauf.

## Syntaxhervorhebung

Die eingebauten Farben:

- reservierte Wörter: hellblau;
- andere Schlüsselwörter: blau, damit `NAME`, `TYPE`, `VALUE` als
  Spaltennamen nicht hervorstechen;
- isql-Befehle: gelb. Das gilt für das Befehlswort (`SHOW`, `HELP`,
  `INPUT`/`IN`, `OUTPUT`/`OUT`, `EDIT`, `SHELL`, `EXIT`, ...), die
  `SET`-Option (`STATS`, `AUTODDL`, `TERM`, `BLOB`, ...), das `SHOW`-Objekt
  (`TABLES`, `DB`, `VERSION`, ...) und den Schalter danach (`ON`/`OFF`,
  `ALL`, `DIALECT`). Die Wortlisten und Abkürzungen sind dieselben, die isql
  akzeptiert (`SET STAT ON`, `SHOW TABLE`);
- `SET` ohne isql-Option (`SET TRANSACTION`, `SET GENERATOR`,
  `SET ROLE`, `SET TIME ZONE`, `SET BIND`) ist SQL und erhält die normalen
  SQL-Farben;
- Strings: grün (auch kyrillischer Text);
- Zahlen: magenta;
- Kommentare: grau;
- der Terminator: rot;
- Parameter `:` und `?`: cyan.

Die Fortsetzungszeilen `CON>` werden unter Berücksichtigung der vorherigen
Zeilen der Anweisung eingefärbt. Zum Beispiel weiß hqisql, dass eine Zeile
innerhalb eines offenen Strings oder Kommentars liegt.

Sie können die Farben mit einer Farbschema-Datei ändern (siehe "Farbschema").

## Tab-Vervollständigung

- Tab-Vervollständigung und Hinweise während der Eingabe: Schlüsselwörter
  sowie Tabellen, Spalten, Prozeduren, Funktionen, Generatoren und Packages
  der verbundenen Datenbank. hqisql lädt die Namen im Hintergrund.
- **Namen mit Groß-/Kleinschreibung und Anführungszeichen.** hqisql speichert
  die Namen so, wie sie sind: `from ord` + Tab ergibt `"Orders"`, `o."I` + Tab
  ergibt `"Id"`. hqisql setzt einen Namen in Anführungszeichen, wenn er ohne
  sie nicht gelesen werden kann (Kleinbuchstaben, Leerzeichen, ein
  reserviertes Wort). Ohne Anführungszeichen ignoriert der Vergleich die
  Groß-/Kleinschreibung. In Anführungszeichen ist der Vergleich exakt.
- **Aktualisierung nach DDL.** Nach `CREATE`, `ALTER`, `DROP`, `RECREATE`,
  `DECLARE` und `COMMIT` liest hqisql die Metadaten vor dem nächsten Prompt im
  Hintergrund neu. Beispiel: Nach `create table "Abc"` ergibt `"Ab` + Tab
  `"Abc"` ohne neue Verbindung.
- **Qualifizierte Vervollständigung.** Nach `Table.` zeigt hqisql nur die
  Spalten dieser Tabelle. Nach `Package.` zeigt es die Prozeduren und
  Funktionen des Packages. hqisql findet `Alias.` im Abfragetext (`FROM`,
  `JOIN`, `UPDATE`, Listen mit Kommas, optionales `AS`, ein Alias in
  Anführungszeichen). Der Kontext umfasst alle Fortsetzungszeilen `CON>` und
  den Text nach dem Cursor bis zum Ende der Anweisung. Daher funktioniert
  `SELECT E.` + Tab auch, wenn `FROM EMPLOYEE E` bereits rechts vom Cursor
  steht. Andere Anweisungen in derselben Zeile werden nicht verwendet.
- **Tabellenposition.** Nach `FROM`, `JOIN` und `UPDATE` zeigt hqisql nur
  Tabellen und Views. Systemtabellen (`RDB$`, `MON$`, `SEC$`, `TMP$`) sind
  ausgeblendet.

## Zeichensätze

hqisql liest und gibt Daten in der Codepage der Konsole aus, wie isql. replxx
schaltet die Windows-Konsole nur während der Bearbeitung einer Zeile auf UTF-8
und VT-Modus um. Vor der Fortsetzung von isql stellt es die alten Werte
wieder her. Die eingegebene Zeile wird in die Eingabe-Codepage der Konsole
umgewandelt. Daher gelten dieselben Regeln wie für isql: `-ch` muss zur
Codepage der Konsole passen (`chcp 1251` + `-ch WIN1251`, `chcp 65001` +
`-ch UTF8`).

Eine WIN1251-Datenbank in der Konsolen-Codepage 1251 und `-ch UTF8` in der
Konsolen-Codepage 65001 ergeben dieselben Bytes wie isql. Dasselbe gilt unter
Linux (UTF-8, `-ch UTF8`).

## PREPARE / EXEC SQL / DEALLOCATE

Diese Befehle gibt es nur in hqisql. Sie funktionieren interaktiv und in
Skripten (`-i`, `INPUT`, Pipe).

```
PREPARE name AS <statement with ? >;
EXEC SQL name USING (v1, v2, ...);
EXEC SQL name USING BATCH (v1, ...), (v1, ...), ...;
DEALLOCATE name;
```

**Dies ist eine Textersetzung im Client, kein Prepare auf dem Server.**
`EXEC SQL` ersetzt jedes `?` (außerhalb von Strings, Namen in
Anführungszeichen und Kommentaren) durch den Wert, so wie er geschrieben ist.
Das Ergebnis ist eine neue Anweisung. Der Server bereitet sie neu vor und
führt sie aus. Daher gilt:

- ein Wert ist ein beliebiger SQL-Ausdruck (`1`, `'text'`, `current_date`,
  `(2 + 3) * 4`); setzen Sie Strings in Anführungszeichen;
- `BATCH` sind N einzelne Anweisungen, nicht `IBatch`;
- der Name gilt bis zum Ende der Sitzung. Ein neues `PREPARE` mit demselben
  Namen ersetzt die Anweisung.

Wie hqisql die Werte liest:

- Strings `'...'` (mit `''`), Q-Strings `q'{...}'`, Namen `"..."` und
  verschachtelte Klammern innerhalb eines Werts bleiben unverändert;
- hqisql entfernt Kommentare aus den Werten (ein Zeilenkommentar würde den
  Rest der Anweisungszeile auskommentieren);
- nach der Liste sind nur Leerzeichen und Kommentare erlaubt. Zusätzlicher
  Text, ein leerer Wert oder eine fehlende `)` ist ein Fehler;
- `()` ist die Liste für eine Anweisung ohne `?`.

Wie hqisql die Anweisung ausführt:

- ein einzelnes `EXEC SQL` schreibt die Anweisung um und übergibt sie an die
  normale isql-Verarbeitung (isql-Befehle, Ausgabe und Transaktionen
  funktionieren wie bei direkter Eingabe);
- `BATCH` prüft zuerst alle Tupel (die Anzahl der Werte). Dann führt es jede
  Anweisung auf dieselbe Weise aus: zuerst die isql-Befehle (`SHOW`, `SET` ...),
  dann SQL. Es stoppt beim ersten Fehler;
- ein Fehler des Befehls selbst (`EXEC SQL: ...`) setzt den Fehler-Exit-Code,
  beachtet `SET BAIL` und gibt in einem Skript "After line N in file F" aus,
  wie isql nach einem Fehler in einer Anweisung.

## Dateien laden: EXEC SQL ... FROM und IMPORT INTO

Diese Befehle gibt es nur in hqisql. Sie funktionieren interaktiv und in
Skripten. Die Syntax `FROM CSV` stammt aus
[FirebirdSQL/firebird#9046](https://github.com/FirebirdSQL/firebird/issues/9046).

```
PREPARE ins AS INSERT INTO ORDERS (ID, CUSTOMER, AMOUNT) VALUES (?, UPPER(?), ?);
EXEC SQL ins FROM CSV 'orders.csv' OPTIONS (HEADER, ERRORS = 10);

IMPORT INTO EMPLOYEE FROM CSV 'emp.csv' OPTIONS (HEADER, DELIMITER = ';');
IMPORT INTO "Orders" ("Id", AMOUNT) FROM CSV 'C:\data\o.csv' OPTIONS (ENCODING = WIN1251, DECIMAL = ',');
```

- **EXEC SQL name FROM CSV**: hqisql führt die Anweisung aus `PREPARE` für
  jeden Datensatz der Datei aus. Feld N ist der Wert des N-ten `?`. Erlaubte
  Anweisungen: `INSERT`, `UPDATE`, `DELETE`, `MERGE`, `EXECUTE PROCEDURE`,
  `EXECUTE BLOCK`. `SELECT` ist ein Fehler.
- **IMPORT INTO table**: hqisql erzeugt das `INSERT` selbst. Die Spalten
  stammen aus der Liste im Befehl oder aus dem Dateikopf (`HEADER`), oder es
  sind alle Spalten der Tabelle in ihrer Reihenfolge, außer berechneten
  Spalten und Spalten `GENERATED ALWAYS AS IDENTITY`. Ein Name aus dem
  Dateikopf: Ein regulärer Bezeichner wird in Großbuchstaben umgewandelt;
  jeder andere Name wird unverändert in Anführungszeichen gesetzt. Ein View
  funktioniert ebenfalls.
- Ein relativer Dateiname ist relativ zum Verzeichnis des Skripts (`-i`,
  `INPUT`). In der Konsole ist er relativ zum aktuellen Verzeichnis.

Funktionsweise:

- der Server bereitet die Anweisung **einmal** vor, in der DML-Transaktion von
  isql (bei `USING` werden die Werte stattdessen in den Text eingesetzt);
- jedes Feld wird als `VARCHAR` im Zeichensatz der Verbindung übergeben (oder
  in `ENCODING`; nach einem UTF-8-BOM in UTF8). Der Server wandelt den Wert in
  den Parametertyp um (Datum, Zahl, BOOLEAN, BLOB), wie bei einem Literal;
- die Datensätze werden in `IBatch`-Batches übertragen (`BATCH`, Puffer
  16 MB). Jeder Batch läuft unter `SAVEPOINT HQ$IMPORT`. Schlägt ein Batch
  fehl, setzt hqisql ihn auf den Savepoint zurück und führt seine Datensätze
  einzeln erneut aus. So erhält der Fehler die genaue Zeilennummer in der
  Datei;
- ein Server ohne `IBatch` (Firebird 3, Protokoll < 16): ein Datensatz nach
  dem anderen, wie bei `BATCH = 0`. Getestet mit Firebird-3.0-Servern über
  TCP: dasselbe Testskript wie für Firebird 5 liefert dasselbe Ergebnis.

### Optionen

| Option | Standard | Bedeutung |
|---|---|---|
| `DELIMITER` | `','` | Feldtrenner; `'\t'` ist ein Tabulator |
| `QUOTE` | `'"'` | Anführungszeichen; `""` innerhalb eines Felds ist das Anführungszeichen selbst |
| `HEADER` | `FALSE` | der erste Datensatz enthält die Spaltennamen; `HEADER` ohne Wert = `TRUE` |
| `SKIP` | `0` | n Datensätze nach dem Kopf überspringen |
| `NULL` | `''` | ein Feld ohne Anführungszeichen mit diesem Text ist NULL; standardmäßig ist ein leeres Feld NULL und `""` ein leerer String |
| `ENCODING` | Zeichensatz der Verbindung | Zeichensatz der Datei (`WIN1251`, `UTF8`, ...) |
| `DECIMAL` | `'.'` | `','` bedeutet ein Komma in Zahlen (`1,5`) |
| `BATCH` | `1000` | Datensätze pro Batch; `0` bedeutet kein `IBatch`, einer nach dem anderen |
| `COMMIT` | `0` | Commit alle n Datensätze und am Ende; `0` bedeutet, dass der Benutzer die Transaktion beendet |
| `ERRORS` | `0` | wie viele fehlerhafte Datensätze übersprungen werden dürfen |

### Fehler

- Ohne `ERRORS` stoppt das Laden beim ersten fehlerhaften Datensatz:
  `file, line N:`, der Serverfehler, `After line M in file script.sql`.
  Die Datensätze davor bleiben in der Transaktion (wie bei `SET BULK_INSERT`).
  Der Exit-Code zeigt einen Fehler an; `SET BAIL` wird beachtet.
- Mit `ERRORS = n`: `file, line N: rejected: message`. hqisql schreibt den
  Datensatz in `<file>.bad` (zuerst die Kopfzeile, falls vorhanden). Beim
  n+1-ten fehlerhaften Datensatz stoppt das Laden.
- Ein Datensatz mit falscher Anzahl von Feldern, ein Feld, das länger als die
  Spalte ist, oder ein nicht geschlossenes Anführungszeichen ergibt einen
  Fehler mit der Zeilennummer. hqisql sendet einen solchen Datensatz nicht an
  den Server.
- Ctrl-C bricht das Laden ab (`cancelOperation`). Die Datensätze des aktuellen
  Batches werden zurückgerollt.

### Zusammenfassung und Geschwindigkeit

Am Ende gibt hqisql `N rows imported, K rejected (path .bad), T s` aus
(`executed` bei `EXEC SQL`). Mit `-q` gibt hqisql dies nicht aus; gibt es
abgelehnte Datensätze, gibt es die Zusammenfassung auf stderr aus. In der
Konsole zeigt hqisql einmal pro Sekunde einen Datensatzzähler.

Geschwindigkeit: 1 Million Datensätze in eine Tabelle
`(INT, VARCHAR(30), NUMERIC(12,2), DATE)`, Firebird 5.0, Laufzeit von hqisql
ohne das Erstellen der Datenbank:

| Methode | Windows 11, Embedded | Windows 11, TCP localhost (SuperServer) |
|---|---|---|
| `IMPORT INTO` (BATCH 1000) | 1,3 s | 2,2 s |
| `IMPORT INTO`, `BATCH = 0` | 2,5 s | 45,9 s |
| `SET BULK_INSERT` | 3,4 s | 70,9 s |
| `EXEC SQL ... USING BATCH` (100.000 Datensätze) | 2,6 s | 21,8 s |

Unter Linux (Ubuntu 22.04, Embedded): `IMPORT INTO` dauert 11,1 s,
`SET BULK_INSERT` dauert 22,3 s.

`EXEC SQL ... USING BATCH` bereitet jede Anweisung neu vor. Für 1 Million
Datensätze dauert es etwa 26 s bzw. 218 s. Über das Netzwerk bringt `IBatch`
den größten Gewinn, weil es keinen Roundtrip zum Server für jeden Datensatz
gibt.

### Formate COPY, PARQUET und AVRO

Die Syntax ist dieselbe. Nur das Formatwort ändert sich:

```
IMPORT INTO T FROM COPY 'dump.txt';
IMPORT INTO T (ID, NAME, AMOUNT) FROM PARQUET 'orders.parquet' OPTIONS (BATCH = 5000);
EXEC SQL ins FROM AVRO 'events.avro' OPTIONS (ERRORS = 10);
```

| Format | Was es ist | Optionen |
|---|---|---|
| `CSV` | RFC 4180 (siehe oben) | alle |
| `COPY` | PostgreSQL-Textformat von `COPY ... TO` / `\copy` / pg_dump-Daten | alle außer `QUOTE`; der Standard für `DELIMITER` ist ein Tabulator, der Standard für `NULL` ist `\N` |
| `PARQUET` | Apache Parquet | `SKIP`, `BATCH`, `COMMIT`, `ERRORS` |
| `AVRO` | Avro Object Container (`.avro`) | `SKIP`, `BATCH`, `COMMIT`, `ERRORS` |

Jede andere Option für ein Format ist ein Parse-Fehler (`option HEADER is not for
PARQUET files`).

**COPY**: Eine Zeile ist ein Datensatz, `\N` ist NULL. Escape-Sequenzen:
`\t \n \r \b \f \v`, `\\`, oktal `\101`, `\x41`. Die Zeile `\.` ist das Ende
der Daten. hqisql erkennt das Binärformat `PGCOPY` und lehnt es ab. `HEADER`,
`ENCODING` und `DECIMAL` funktionieren wie bei CSV. hqisql wandelt die
Boolean-Werte `t` / `f` und den Offset `+03` in `timestamptz` in eine Form um,
die Firebird versteht.

**PARQUET und AVRO**: Die Datei hat ein Schema, daher werden die Spalten
**nach Namen** zugeordnet:

- `IMPORT INTO t (a, b)`: Die Spalten `a`, `b` der Datei gehen in die Spalten
  `A`, `B` der Tabelle. Ohne Liste: alle Spalten der Datei; der Name einer
  Dateispalte ist der Name der Tabellenspalte (`id` → `ID`). Ein Name ohne
  Anführungszeichen wird ohne Rücksicht auf Groß-/Kleinschreibung gefunden.
  Gibt es keine solche Spalte, zeigt der Fehler die Liste der Dateispalten;
- `EXEC SQL name`: alle Spalten der Datei in ihrer Reihenfolge. Ihre Anzahl
  muss gleich der Anzahl der `?` sein;
- Parquet: Eine Spalte einer verschachtelten Gruppe hat den Namen
  `group.column`. Listen und Maps (wiederholte Spalten) können nicht geladen
  werden; die Auswahl einer solchen Spalte ist ein Fehler;
- Avro: die Felder des Records auf oberster Ebene. Eine Union mit null ist eine
  Spalte mit NULL. record, array und map werden zu JSON-Text. Ist das Schema
  kein record, hat die Datei eine Spalte `VALUE`.

Die Werte werden als Text übergeben, wie bei CSV, mit diesen Ausnahmen:

- Bytes (Parquet `BYTE_ARRAY` ohne logischen Typ, `FIXED_LEN_BYTE_ARRAY`;
  Avro `bytes`, `fixed`) gehen als `OCTETS` ohne Umwandlung in `VARBINARY` /
  `CHAR(n) CHARACTER SET OCTETS` / `BLOB`;
- eine UUID geht als 16 Bytes in eine `OCTETS`-Spalte, sonst als Text
  `12345678-9abc-def0-1234-56789abcdef0`;
- eine Zeit in UTC (Parquet `TIMESTAMP(isAdjustedToUTC)`, nicht INT96; Avro
  `timestamp-*`) erhält ` +00:00` für eine Spalte `WITH TIME ZONE`. Für eine
  Spalte `TIMESTAMP` ist der Wert UTC ohne Zone;
- der Sekundenbruchteil wird auf 4 Stellen gekürzt (die Genauigkeit von
  Firebird). `DECIMAL` ist eine exakte Zahl; `FLOAT` / `DOUBLE` verwenden die
  kürzeste exakte Form.

Parquet: Pages v1 und v2, Dictionaries, die Encodings PLAIN, RLE, BIT_PACKED,
DELTA_BINARY_PACKED, DELTA_LENGTH_BYTE_ARRAY, DELTA_BYTE_ARRAY,
BYTE_STREAM_SPLIT; die Kompressionen SNAPPY, GZIP, LZ4, LZ4_RAW, ZSTD (BROTLI
und LZO ergeben einen Fehler). Avro: die Codecs null, deflate, snappy,
zstandard (bzip2 und xz ergeben einen Fehler).

hqisql hat einen eigenen Reader für diese Formate und benötigt keine
zusätzlichen Bibliotheken, außer für komprimierte Daten. Es lädt zlib und zstd
beim ersten komprimierten Block: `zlib1.dll` (das hqisql-Verzeichnis, dann
`PATH`) / `libz.so.1`, und `libzstd.dll` oder `zstd.dll` / `libzstd.so.1`.
Fehlt die Bibliothek, lautet der Fehler `the zstd compression needs ...`.
hqisql liest die Datei seitenweise (Parquet) und blockweise (Avro). Es lädt
nicht die ganze Datei in den Speicher.

In den Meldungen und in `.bad` ist die Datensatznummer `file, row N`. In
`.bad` ist der Datensatz eine CSV-Zeile; die erste Zeile enthält die
Spaltennamen.

Getestet mit Firebird 5 (Embedded und TCP) und mit Firebird-3.0-Servern über
TCP (ein Datensatz nach dem anderen): Parquet (snappy, zstd v2, gzip), Avro
(null, deflate), COPY. 200.000 Parquet-Zeilen (zstd) werden in etwa 1 s in ein
Embedded-Firebird 5 geladen und in etwa 40 s in Firebird 3.0 über TCP.

### Editor-Unterstützung und HELP

Der Editor färbt `PREPARE`, `EXEC SQL`, `DEALLOCATE`, `IMPORT INTO`,
`FROM CSV` (und `COPY`, `PARQUET`, `AVRO`), `OPTIONS` und die Optionsnamen in
der Farbe der isql-Befehle. Tab-Vervollständigung: nach `IMPORT INTO` Tabellen
und Views; nach `FROM` die Formate; innerhalb von `OPTIONS (` die
Optionsnamen; nach `EXEC SQL` und `DEALLOCATE` die Namen aus `PREPARE`.
`HELP` zeigt die hqisql-Befehle und -Optionen.

## SET PAGELENGTH

Diesen Befehl gibt es nur in hqisql. In isql können Sie die Anzahl der Zeilen,
nach denen die Spaltenköpfe und `====` erneut ausgegeben werden, nur mit der
Kommandozeilenoption `-pag <n>` festlegen.

```
SET PAGELENGTH <n>;   -- header every n rows (short form: SET PAG n)
SET PAGELENGTH 0;     -- header once per query
SET PAGELENGTH;       -- default, 20
```

Ein negativer Wert oder ein Wert, der keine Zahl ist, ist ein Fehler. `SET`
zeigt den aktuellen Wert (`Page length:`). Der Befehl ist in `HELP SET`
aufgeführt. Anders als `-pag 0` schaltet `SET PAGELENGTH 0` `SET HEADING`
nicht aus.

## SET PREVIEW

Diesen Befehl gibt es nur in hqisql. Er schaltet den Vorschaumodus ein. hqisql
gibt das Abfrageergebnis als kompakte Tabelle aus, ohne Umbruch der Köpfe und
Zeilen:

```
SET PREVIEW ON;       -- switch on (SET PREVIEW without an argument toggles)
SET PREVIEW 50;       -- switch on and show the first 50 rows (default 20)
SET PREVIEW OFF;
```

Was er bewirkt:

- hqisql holt nur die ersten N Zeilen (Fetch). Der Fetch stoppt, und der
  Server liest nicht weiter. Wurde das Limit erreicht, gibt hqisql nach der
  Tabelle `-- preview: first 20 rows` aus;
- die Breite jeder Spalte ist die Breite des längsten Werts oder Kopfs, nicht
  die deklarierte Länge. Zahlen bleiben rechtsbündig; `<null>` wird wie in
  isql angezeigt;
- Spalten, die nicht in die Bildschirmbreite passen, werden rechts
  weggelassen. Die letzte angezeigte Spalte kann gekürzt werden (auf nicht
  weniger als 4 Zeichen). Die Liste der weggelassenen Spalten steht in
  derselben Zeile: `-- preview: ... 2 more columns: C3 C4`. hqisql nimmt die
  Bildschirmbreite von der Konsole. Geht die Ausgabe nicht an ein Terminal
  (`OUTPUT file`, Pipe), hat die Breite kein Limit, aber die Spalten werden
  trotzdem schmal gemacht;
- `SET LIST ON` schaltet den Modus für die Abfrage aus. `SET HEADING OFF`
  entfernt die Kopfzeilen. hqisql gibt nur den Bezeichner eines BLOB-Felds aus
  (im Vorschaumodus gibt es den BLOB-Inhalt nicht aus). `SET COUNT ON` zählt
  die geholten Zeilen.

Breiten werden in Zeichen gezählt (bei einer UTF8-Verbindung in Codepoints,
sonst in Bytes). `SET` zeigt den aktuellen Zustand (`Preview: ON, 20 rows`).

## Farbschema

Eine optionale Textdatei legt die Farben der Hervorhebung fest. hqisql liest
sie einmal beim Start (nur wenn der Editor aktiv ist und `NO_COLOR` nicht
gesetzt ist):

- `%LOCALAPPDATA%\hqisql\colors.txt` unter Windows, `~/.hqisql_colors` unter
  POSIX (neben der Verlaufsdatei);
- die Variable `HQISQL_COLORS=<path>` gibt eine andere Datei an. Existiert
  diese Datei nicht, gibt hqisql eine Warnung auf stderr aus und verwendet die
  eingebauten Farben.

Ohne Datei verwendet hqisql die eingebauten Farben. Die Beispieldatei
[hqisql-colors.txt](../hqisql-colors.txt) enthält die eingebauten Farben und
beschreibt das Format. Sie können sie kopieren und ändern. Das Format ist eine
Zeile pro Element; `#` beginnt einen Kommentar:

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

Farben:

- `default`: die Textfarbe des Terminals. `default` ohne Attribute bedeutet
  "nicht einfärben";
- die 16 replxx-Namen: `black red green brown blue magenta cyan lightgray
  gray brightred brightgreen yellow brightblue brightmagenta brightcyan
  white`;
- `rgb(r,g,b)` mit Komponenten 0..5 (der Würfel mit 216 Farben);
- `gray(n)` mit n von 0 bis 23;
- eine Zahl von 0 bis 255 (der Index in der Palette des Terminals).

Bei Namen spielt die Groß-/Kleinschreibung keine Rolle. Das Trennzeichen ist
`=` oder `:`. Ein Element, das nicht in der Datei steht, behält seine
eingebaute Farbe. hqisql überspringt eine Zeile, die es nicht versteht, und
gibt eine Meldung aus, zum Beispiel
`hqisql: color scheme <file>: line N: unknown color 'pink'`. Die Farben 16..255
benötigen ein Terminal mit 256 Farben (Windows Terminal, conhost unter
Windows 10 und neuer, xterm-256color).

## Build

hqisql wird nicht allein gebaut. Sie installieren es in einen
Firebird-5.0-Quellbaum und bauen es zusammen mit Firebird. hqisql ist mit
Firebird `v5.0.4` und `v5.0-release` (dem kommenden 5.0.5) getestet.

Sie benötigen:

- git. `install.sh` verwendet `git apply`. Der Firebird-Baum muss kein
  git-Arbeitsverzeichnis sein.
- Windows: Git for Windows (`install.bat` führt `install.sh` mit dessen bash
  aus) und Visual Studio 2022. Mit Firebird `v5.0-release` können Sie auch
  Visual Studio 2026 verwenden; Firebird 5.0.4 kennt Visual Studio 2026 nicht.

### Linux

```
git clone -b v5.0.4 https://github.com/FirebirdSQL/firebird
git clone https://github.com/IBSurgeon/hqisql
hqisql/install.sh firebird && cd firebird && ./autogen.sh && make -j8 && make run_tests
```

Das Ergebnis ist `gen/Release/firebird/bin/hqisql`. `make tests` und
`make run_tests` bauen und starten die Unit-Tests `hqisql_test` zusammen mit
den Firebird-Tests.

### Windows

```
hqisql\install.bat C:\src\firebird
```

Führen Sie danach in `builds\win32` die Dateien `make_icu.bat`,
`make_boot.bat` und `make_all.bat` (oder `run_all.bat`) aus. `make_all.bat` baut hqisql (die
Solution `HQisql.sln`), startet `hqisql_test` und kopiert `hqisql.exe` und
`replxx-LICENSE.md` nach `output_<platform>`.

### Installationsoptionen

`install.sh` und `install.bat` haben diese Optionen:

- `--check`: nur prüfen, ob sich die Patches anwenden lassen. Nichts wird
  geändert.
- `--remove`: die Patches rückgängig machen und die Dateien löschen.
- `--no-hook`: die Build-Dateien von Firebird nicht ändern. Bauen Sie hqisql
  dann nach Firebird mit
  `make -C gen -f ../builds/posix/Makefile.hqisql` (Linux) oder
  `builds\win32\make_hqisql.bat` (Windows).

Führen Sie die Installation erneut mit derselben Version aus, tut sie nichts.
Eine neue Version ersetzt die alte. Die Installationsinformation steht in
`src/hqisql/.install`.

Was die Installation im Firebird-Baum ändert:

- sie kopiert `src/hqisql`, `extern/replxx` und die Build-Dateien von hqisql;
- sie patcht `src/isql/isql.epp`. Alle Änderungen stehen unter
  `#ifdef HQISQL`, daher ändert sich das normale isql nicht;
- ohne `--no-hook` patcht sie `builds/posix/Makefile.in` und
  `builds/win32/make_all.bat`.

### Laufzeit

- hqisql benötigt den fbclient von Firebird 5. Der Server kann Firebird 3.0
  oder neuer sein.
- Mit zstd komprimierte Parquet- und Avro-Dateien benötigen libzstd. Linux:
  das Systempaket (zum Beispiel `libzstd1`). Windows: `libzstd.dll` neben
  `hqisql.exe`; das Release-Archiv enthält sie.
- zlib wird mit Firebird geliefert.
- `hqisql -z` gibt `hqisql 1.0.0` aus, danach die Versionszeile von isql.

## Integrationstests

```
python3 tests/integration/run.py <firebird-dir>
```

Das Skript führt die Tests für `IMPORT` und `EXEC SQL` auf einer
Embedded-Datenbank aus und vergleicht die Ausgabe mit
`tests/integration/expected/*.out`. Es benötigt pyarrow, um die Parquet- und
Avro-Daten zu erzeugen.

## Bekannte Einschränkungen

- macOS wird nicht gebaut und nicht getestet.
- Dateien laden: BLOBs größer als 32 KB, `FROM STDIN`, Export nach CSV und das
  Erstellen einer Tabelle aus dem Dateikopf werden nicht unterstützt.
- Es gibt keinen externen Editor (Alt+E), keine Hervorhebung der Fehlerposition
  vom Server und keinen vi-Modus.

## Funktionsweise

Dieser Abschnitt ist für Mitwirkende.

hqisql wird aus den isql-Quellen als zweites Target gebaut. Der Build
kompiliert `isql.epp` und die anderen isql-Quellen erneut mit `-DHQISQL` und
fügt die Dateien aus `src/hqisql` hinzu. `isql.epp` enthält nur dünne Hooks
unter `#ifdef HQISQL`. Die Schnittstelle der Hooks ist `src/hqisql/HqIsql.h`;
das ist der einzige hqisql-Header, den `isql.epp` einbindet. Ohne `HQISQL` ist
das vom Präprozessor erzeugte `isql.cpp` dasselbe wie in Firebird, daher
ändert sich isql nicht.

Die Eingabe verzweigt sich an einer Stelle: `readNextInputLine()` in
`isql.epp`. Der Hook steht vor dem Zweig `HAVE_EDITLINE_H`. Gibt
`Hq::readLine()` false zurück (`-noedit`, kein Terminal, `-i`), läuft der
normale isql-Code, einschließlich editline. Im interaktiven hqisql liest
replxx die Zeile. hqisql teilt eine mehrzeilige Eingabe in Zeilen auf, und der
vorhandene Anweisungsleser von isql (`get_statement`) arbeitet ohne
Änderungen. ECHO, die Zeilennummern von Fehlern, `INPUT`, `SET TERM` und die
temporäre Datei funktionieren wie in isql.

Die Module in `src/hqisql`:

- `HqLineReader.cpp`: der replxx-Wrapper (Hervorhebung, Vervollständigung,
  Hinweise, Verlauf, Ctrl-C, Einfügen) und der Code der Hooks;
- `HqLexer`, `HqKeywords`, `HqIsqlCommands`, `HqColors`: Hervorhebung und das
  Farbschema;
- `HqMetadata`: lädt die Namen der Datenbank für die Vervollständigung in
  einem Hintergrund-Thread, in einer eigenen Read-only-Transaktion;
- `HqStatements`, `HqParams`: `PREPARE`, `EXEC SQL`, `DEALLOCATE`;
- `HqImportCommand`, `HqImport`, `HqRows`, `HqCsv`, `HqCopy`, `HqParquet`,
  `HqAvro`, `HqCodec`: Laden von Dateien;
- `HqTable`, `HqPreview`: `SET PREVIEW`;
- `tests/`: die Unit-Tests mit Boost.Test (`hqisql_test`).

replxx liegt in `extern/replxx`. Es ist eine Kopie von ClickHouse/replxx; die
lokalen Änderungen sind in `extern/replxx/REVISION` aufgeführt.
