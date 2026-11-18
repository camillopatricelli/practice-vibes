# 1. Installare Practice Vibes

Practice Vibes è software libero (GNU AGPLv3): si scarica gratis, senza account. Tutti i file sono nella pagina
dell'[ultima build](https://github.com/camillopatricelli/practice-vibes/releases/tag/latest-build):

| Piattaforma | File |
|---|---|
| iPad (iPadOS 16 o successivo) | **PracticeVibes.ipa**, installato con SideStore |
| Windows 10 / 11 (64 bit) | **PracticeVibes-windows.zip** |
| Mac (Apple Silicon o Intel, macOS 12 o successivo) | **PracticeVibes-macos.dmg** |

L'app non è firmata con certificati da sviluppatore a pagamento, quindi ogni sistema chiede conferma una volta prima
di aprirla. I passaggi qui sotto spiegano come.

Cosa ti serve oltre all'app:
- **Un'interfaccia audio** per il tuo strumento (USB, o un adattatore tipo iRig su iPad). Il microfono integrato va
  bene per l'accordatore, ma sente anche la stanza, e il diario di studio conterebbe il rumore di fondo come se
  suonassi.
- **Cuffie.** Se ascolti il tuo strumento attraverso l'app dagli altoparlanti mentre usi un microfono, c'è il larsen.

Di più su cosa collegare: **Configurazione audio** (AUDIO-SETUP.md).

---

## iPad

L'app non è sull'App Store. Si installa con **SideStore**, uno strumento gratuito che firma le app con il tuo Apple ID
(ne basta uno gratuito) e le installa sull'iPad. Ti serve un computer una volta sola, per la prima configurazione;
dopo, tutto avviene sull'iPad.

> Gli strumenti e i passaggi di SideStore cambiano ogni tanto. Se qualcosa qui sotto è diverso, segui la guida
> ufficiale su [docs.sidestore.io](https://docs.sidestore.io) (in inglese).

### La prima volta: configura SideStore (circa 30 minuti, una volta)

**Sull'iPad**
1. Aggiorna iPadOS: **Impostazioni › Generali › Aggiornamento software**.
2. Imposta un codice, collegati al Wi-Fi e accedi con il tuo Apple ID. L'autenticazione a due fattori deve essere
   attiva.

**Sul computer**
3. Windows: installa i driver Apple con l'app **Dispositivi Apple** (Microsoft Store) o con **iTunes**, altrimenti
   Windows non vede l'iPad. Mac: non serve installare nulla.
4. Collega l'iPad con il cavo e sbloccalo. Tocca **Autorizza** quando chiede se fidarti del computer e inserisci il
   codice.
5. Avvia il programma di installazione consigliato dalla guida di SideStore (nel 2026: **iloader**), accedi con il tuo
   Apple ID e installa SideStore sull'iPad. Questo crea anche il "file di abbinamento" che servirà a SideStore.

**Di nuovo sull'iPad**
6. **Impostazioni › Generali › VPN e gestione dispositivi**: autorizza il profilo sviluppatore con il tuo Apple ID.
7. **Impostazioni › Privacy e sicurezza › Modalità sviluppatore**: attivala. L'iPad si riavvia. (L'opzione compare
   solo dopo aver installato la prima app esterna all'App Store.)
8. Installa **LocalDevVPN** dall'App Store. SideStore ne ha bisogno, attiva, per installare e rinnovare le app.
9. Apri **SideStore** e accedi con lo stesso Apple ID.

Ora puoi scollegare l'iPad.

### Installa Practice Vibes

1. In Safari, apri la pagina dell'[ultima build](https://github.com/camillopatricelli/practice-vibes/releases/tag/latest-build)
   e scarica **PracticeVibes.ipa** (finisce in **File › Download**).
2. Attiva **LocalDevVPN**, apri **SideStore**, vai in **My Apps**, tocca **+** e scegli il file.
3. Quando è installata, spegni LocalDevVPN e apri Practice Vibes. Consenti **Microfono** (l'ingresso del tuo strumento)
   e **Bluetooth** (footswitch) quando te lo chiede.

### Ogni 7 giorni: rinnova

Le app firmate con un Apple ID gratuito smettono di aprirsi 7 giorni dopo l'ultima firma. I tuoi dati non si perdono,
ma l'app non si apre finché non la rinnovi:
1. Attiva **LocalDevVPN**.
2. Apri **SideStore** › **My Apps** › **Refresh All**.
3. Spegni di nuovo la VPN.

Meglio rinnovare ogni pochi giorni, per sicurezza.

### Aggiornare a una nuova build

Scarica il nuovo **PracticeVibes.ipa** e installalo allo stesso modo (passi 1–3 qui sopra). **Installa sopra l'app
vecchia: non cancellarla prima.** Installando sopra mantieni impostazioni e dati; cancellandola perdi tutto quello che
è salvato sull'iPad. Una libreria in iCloud Drive è al sicuro in ogni caso (vedi capitolo 9). Il numero di build è in
**Impostazioni audio › Informazioni su Practice Vibes**.

### Buono a sapersi
- Un Apple ID gratuito può avere al massimo **3 app esterne** alla volta (SideStore conta come una) e 10 nuovi ID app a
  settimana. Aggiornare Practice Vibes non ne usa di nuovi.
- Il computer ti serve di nuovo solo se SideStore smette di funzionare con un errore di abbinamento o di connessione
  (per esempio dopo un aggiornamento di iPadOS): collega il cavo, avvia **iloader** e rinnova il file di abbinamento.

---

## Windows

1. Scarica **PracticeVibes-windows.zip** dalla pagina dell'[ultima build](https://github.com/camillopatricelli/practice-vibes/releases/tag/latest-build)
   ed estrailo in una cartella a tua scelta (per esempio `Documenti\Practice Vibes app`). Tieni tutti i file insieme:
   `onnxruntime.dll` serve per la separazione degli stem.
2. Fai doppio clic su **Practice Vibes.exe**. Se Windows mostra "PC protetto da Windows", fai clic su **Ulteriori
   informazioni** › **Esegui comunque**. Succede solo la prima volta.
3. Facoltativo: clic destro sul file .exe › **Mostra altre opzioni** › **Invia a** › **Desktop (crea collegamento)**.

Windows 10: documenti e YouTube si aprono in un visualizzatore interno che usa Microsoft Edge **WebView2**. È incluso
in Windows 11; su Windows 10, se documenti o video restano vuoti, installa gratis **WebView2 Runtime** da Microsoft.

**Aggiornare:** chiudi l'app, poi estrai la nuova versione sopra la cartella vecchia. Le impostazioni e la libreria
sono salvate altrove, quindi non vengono toccate.

> **Consiglio:** installa il driver della tua interfaccia audio dal sito del produttore, se c'è. Il capitolo 2 spiega
> come ridurre al minimo il ritardo tra quando suoni e quando ti senti.

---

## Mac

1. Scarica **PracticeVibes-macos.dmg** dalla pagina dell'[ultima build](https://github.com/camillopatricelli/practice-vibes/releases/tag/latest-build)
   e aprilo.
2. Trascina **Practice Vibes** sulla cartella **Applicazioni** in quella finestra.
3. Apri Practice Vibes da Applicazioni. L'app non è autenticata da Apple, quindi la prima volta macOS la blocca:
   - macOS 15 o successivo: **Impostazioni di Sistema › Privacy e sicurezza**, scorri fino a "Practice Vibes è stata
     bloccata" e fai clic su **Apri comunque**, poi conferma.
   - macOS da 12 a 14: clic destro (o Ctrl-clic) sull'app › **Apri** › **Apri**.
   Il file **READ ME FIRST.txt** nel .dmg ripete gli stessi passaggi (in inglese).
4. Consenti l'accesso al **microfono** quando te lo chiede. Dopo un aggiornamento macOS può chiederlo di nuovo.

**Aggiornare:** trascina la nuova versione in Applicazioni e scegli **Sostituisci**. Una nuova build è una nuova app
non autenticata, quindi macOS può bloccarla di nuovo: consentila allo stesso modo (passo 3).

---

## Dove l'app tiene i suoi file

| | Impostazioni e log | La tua libreria (predefinita) |
|---|---|---|
| iPad | dentro l'app | File › Sul mio iPad › Practice Vibes |
| Windows | `%APPDATA%\Practice Vibes` | scelta al primo avvio |
| Mac | `~/Library/Application Support/Practice Vibes` | scelta al primo avvio |

La libreria (argomenti, brani, take, diario di studio, piani) è una normale cartella, e puoi spostarla in iCloud Drive
per condividerla tra i dispositivi. Vedi il capitolo 9.

Avanti: **[2. Prima configurazione](02-first-setup.md)**: collega lo strumento e riduci il ritardo.
