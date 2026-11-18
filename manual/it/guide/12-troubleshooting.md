# 12. Risoluzione dei problemi

---

## Non sento il mio strumento
- **Monitor ingresso** è attivo? Parte spento ogni volta (scheda Rig, capitolo 2).
- L'**indicatore di ingresso** nella scheda Rig si muove quando suoni? Se no, controlla l'ingresso nelle **Impostazioni
  audio** e i suoi **Active input channels**, e il riquadro dell'ingresso nella scheda Rig.
- L'indicatore si muove ma c'è ancora silenzio: controlla **Livello in uscita**, e che nelle Impostazioni audio sia
  scelto l'**Output** giusto.
- Un plugin nella catena è muto o regolato male? Metti in **Bypass** uno alla volta per scoprirlo.

## C'è un ritardo tra quando suono e quando sento
È la latenza. Usa una **Audio buffer size** più piccola, cuffie con il filo e, su Windows, una modalità del dispositivo
a latenza più bassa (capitolo 2). Le cuffie Bluetooth aggiungono sempre un ritardo lungo.

## Crepitii, clic o interruzioni
Il dispositivo non ce la fa: usa un buffer più grande, chiudi le altre app, collega il caricatore del portatile e
controlla i plugin pesanti (capitoli 2 e 10).

## Il diario di studio conta tempo in cui non suonavo (o perde tempo in cui suonavo)
- Ascolta l'ingresso dello strumento. Con il microfono integrato sente anche la stanza: usa la tua interfaccia audio.
- Un **Gain in ingresso** molto basso può far passare inosservato un modo di suonare delicato.
- Correggi una voce sbagliata toccandola (capitolo 3).

## Gli overdub nel looper suonano in ritardo o "sbavati"
Rifai **Calibra...** nella scheda Looper (capitolo 7), soprattutto dopo aver cambiato interfaccia o dimensione del
buffer.

## Gli accordi sembrano sbagliati
Il riconoscimento degli accordi è una stima automatica. Mix densi, chitarre distorte e accordi insoliti possono uscire
semplificati. Fidati delle tue orecchie.

## Un plugin manca dall'elenco
- **Windows / Mac**: installalo come VST3 (o AU su Mac), poi **Scansiona VST3**.
- **iPad**: tocca **Aggiorna elenco**. Controlla che il plugin compaia in un'altra app (per esempio GarageBand).

## Le modifiche dall'altro dispositivo non compaiono
- Entrambi i dispositivi devono usare la **stessa cartella** in iCloud Drive (o la stessa cartella sincronizzata tra
  computer).
- Aspetta che il cloud sincronizzi, poi **Cartella della libreria...** › **Controlla subito le modifiche**.
- Sull'iPad, per una libreria condivisa si può usare solo iCloud Drive.

## Un video di YouTube non parte
Il proprietario del video può non permetterne la riproduzione in altre app: usa **Apri su YouTube**. Sull'iPad, tocca
una volta il video stesso per la prima riproduzione.

## Windows: "PC protetto da Windows"
Fai clic su **Ulteriori informazioni** › **Esegui comunque** (capitolo 1). Compare solo la prima volta che apri una
nuova versione.

## Mac: "Impossibile aprire Practice Vibes" o "è stata bloccata"
L'app non è autenticata da Apple. Consentila una volta in **Impostazioni di Sistema › Privacy e sicurezza › Apri
comunque** (macOS 15 o successivo), oppure clic destro sull'app › **Apri** (macOS da 12 a 14). Il capitolo 1 ha i
passaggi; una nuova build può chiederlo di nuovo.

## iPad: l'app non si apre più
Probabilmente sono passati più di 7 giorni da quando SideStore l'ha firmata l'ultima volta. Attiva **LocalDevVPN**,
apri **SideStore** › **My Apps** › **Refresh All**. I tuoi dati sono ancora lì. Se il rinnovo non riesce per un errore
di abbinamento o di connessione, vedi "Buono a sapersi" nel capitolo 1.

---

## Il file di log

Quando segnali un problema, il log dell'app aiuta molto. Registra cosa ha fatto l'app ed eventuali errori.

- **Dentro l'app**: **Impostazioni audio** › **Mostra la scheda Log**. Compare la scheda **Log**; **Copia log** lo
  copia così puoi incollarlo in un messaggio.
- **Il file**:
  - iPad: File › Sul mio iPad › Practice Vibes
  - Windows: `%APPDATA%\Practice Vibes` (incollalo nella barra degli indirizzi di Esplora file)
  - Mac: `~/Library/Application Support/Practice Vibes/PracticeVibes.log` (Finder › Vai › Vai alla cartella…)

Descrivi cosa hai fatto, cosa ti aspettavi e cosa è successo, e allega il log.

## Segnalare un problema

Apri una segnalazione (issue) sulla pagina GitHub del progetto (github.com/camillopatricelli/practice-vibes/issues),
oppure scrivi a practicevibes.app@gmail.com. In inglese o in italiano, va bene tutti e due. Prima copia le info
diagnostiche: **Impostazioni audio** › **Informazioni su Practice Vibes** › **Copia info diagnostiche**, poi incollale
nella segnalazione. Contengono la versione e il numero di build, il tuo dispositivo e la tua configurazione audio.

La scheda **Informazioni** mostra anche la versione, i link all'informativa sulla privacy, alla licenza e al codice
sorgente, e **Licenze open source**: le licenze delle librerie, dei font e dei suoni con cui è fatta l'app.
