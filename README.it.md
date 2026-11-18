# Practice Vibes

[🇬🇧 English](README.md) | 🇮🇹 Italiano

Un'app per esercitarsi, per chitarristi, bassisti e altri musicisti, su **iPad, Windows e macOS**.
Software libero con licenza [GNU AGPLv3](LICENSE): gratis, senza account, senza pubblicità, senza tracciamento.

Riunisce in un posto solo quello per cui di solito servono diverse app: un lettore che rallenta i brani e li mette in
loop, il riconoscimento degli accordi, un metronomo, un looper con drum machine, un accordatore, i tuoi plugin di
effetti, una pedaliera MIDI e uno spazio per le tue lezioni e i tuoi appunti. E tiene il conto di quanto suoni davvero.

> **Stato: in pieno sviluppo.** I download qui sotto sono build di prova automatiche, non versioni stabili. Aspettati
> qualche imperfezione, e per favore [segnala quello che trovi](../../issues).

<!-- Screenshot da aggiungere (vedi la lista degli screenshot): scheda Oggi, scheda Studio con accordi e loop, Looper
     con batteria, Accordatore, Argomenti, Rig con plugin. -->

## Per chi è

- **Chi studia da solo**: il tempo di studio viene registrato da solo mentre suoni, con una rotazione degli argomenti,
  piani di sessione, metronomo e loop.
- **Insegnanti**: invii una lezione (appunti, brani, compiti) come un unico file e vedi cosa torna indietro.
- **Musicisti che vogliono un solo strumento**: looper e drum machine, accordatore, separazione in stem, rig di plugin,
  pedaliera.

## Cosa fa

- **Lettore di brani**: WAV, AIFF, FLAC, MP3, M4A e video di YouTube. Velocità dal 25 al 150 % senza cambiare
  l'intonazione, intonazione ±12 semitoni, loop A-B agganciato alle battute, zoom della forma d'onda.
- **Stem** (Windows, macOS): divide un brano in tracce separate (voce, chitarra, basso, batteria, piano e altro), poi
  puoi silenziarle, ascoltarle da sole o bilanciarle.
- **Accordi, beat e tempi in chiave** riconosciuti automaticamente e mostrati mentre suoni.
- **Metronomo** con suddivisioni, swing, diversi suoni, aumento graduale del tempo e "segui i beat del brano".
- **Looper a 4 tracce** agganciato al metronomo, con sovraincisione e annulla, calibrazione della latenza e una **drum
  machine** integrata (kit acustico o elettronico, editor a step).
- **Accordatore**: ago o strobo, accordature per chitarra e basso, tutte le corde in una pennata.
- **Diario di studio**: conta solo i minuti in cui suoni davvero (audio o tastiera MIDI), con cronologia, rotazione e
  piani di sessione. Niente serie da mantenere, niente notifiche.
- **Argomenti**: le tue lezioni con appunti, tag, brani, PDF e link, in una cartella che scegli tu (mettila in iCloud
  Drive o in un'altra cartella cloud per usarla su tutti i tuoi dispositivi). Condividi un argomento come un unico file.
- **Rig**: i tuoi plugin di effetti e strumenti (AUv3 su iPad, VST3 su Windows, Audio Unit e VST3 su macOS).
- **Footswitch**: qualsiasi pedaliera MIDI via USB o Bluetooth, con un'azione al tocco e una tenendo premuto.
- Tema scuro e chiaro; italiano e inglese.

## Download

Dalla pagina dell'[ultima build](../../releases/tag/latest-build):

| Piattaforma | File |
|---|---|
| Windows 10/11 (64 bit) | [PracticeVibes-windows.zip](../../releases/download/latest-build/PracticeVibes-windows.zip) |
| macOS 12 o successivo (Apple Silicon e Intel) | [PracticeVibes-macos.dmg](../../releases/download/latest-build/PracticeVibes-macos.dmg) |
| iPad (iPadOS 16 o successivo) | [PracticeVibes.ipa](../../releases/download/latest-build/PracticeVibes.ipa) (si installa con SideStore, vedi sotto) |

L'app non è firmata con certificati da sviluppatore a pagamento, quindi ogni sistema ti avvisa la prima volta. È
normale:

**Windows.** Estrai la cartella dove vuoi e avvia `Practice Vibes.exe`. Se SmartScreen dice "PC protetto da Windows",
fai clic su **Ulteriori informazioni**, poi su **Esegui comunque**. Il visualizzatore interno usa Microsoft Edge
WebView2, incluso in Windows 11 (su Windows 10, se i documenti non si aprono, installa WebView2 Runtime da Microsoft).

**macOS.** Apri il .dmg e trascina Practice Vibes in Applicazioni. L'app non è autenticata da Apple, quindi il primo
avvio viene bloccato: su macOS 15 o successivo vai in Impostazioni di Sistema > Privacy e sicurezza e fai clic su
**Apri comunque**; su macOS 12-14 fai clic destro sull'app e scegli **Apri**. Il file `READ ME FIRST.txt` nel .dmg
spiega i dettagli (in inglese).

**iPad.** L'app non è sull'App Store. Si installa con [SideStore](https://sidestore.io), che firma le app con il tuo
Apple ID gratuito:
1. Configura SideStore seguendo [la sua guida ufficiale](https://docs.sidestore.io) (serve un computer una volta).
2. Sull'iPad, apri in Safari la pagina dell'[ultima build](../../releases/tag/latest-build) e scarica `PracticeVibes.ipa`.
3. Attiva LocalDevVPN (fa parte della configurazione di SideStore), poi in SideStore vai in **My Apps**, tocca **+** e
   scegli il file scaricato.

Le app installate con un Apple ID gratuito vanno rinnovate ogni 7 giorni: in SideStore, **My Apps** > **Refresh All**
(con LocalDevVPN attivo). Per aggiornare, installa il nuovo .ipa sopra l'app vecchia; non cancellare prima l'app, o se
ne vanno anche i suoi dati.

Al primo avvio, consenti l'accesso al microfono: serve per l'ingresso del tuo strumento. Un'interfaccia audio è la
scelta migliore; il microfono integrato del dispositivo sente anche la stanza.

## Problemi e idee

Apri una [segnalazione](../../issues) (anche in italiano). Nell'app, **Impostazioni audio > Informazioni su Practice
Vibes > Copia info diagnostiche** copia la versione e i dati del dispositivo da incollare nella segnalazione. Il file
di log si trova in:
- Windows: `%APPDATA%\Practice Vibes\PracticeVibes.log`
- macOS: `~/Library/Application Support/Practice Vibes/PracticeVibes.log`
- iPad: app File > Sul mio iPad > Practice Vibes

Altre informazioni in [CONTRIBUTING.md](CONTRIBUTING.md) (in inglese).

## Privacy

L'app non ha account e non invia nulla allo sviluppatore. I tuoi dati restano nella cartella che scegli tu.
I dettagli sono in [PRIVACY.md](PRIVACY.md) (in inglese).

## Compilare dal codice sorgente

Le istruzioni di compilazione sono nel [README in inglese](README.md#building-from-source).

## Licenza e riconoscimenti

Practice Vibes è software libero: puoi usarlo, studiarlo, condividerlo e modificarlo secondo la
[GNU Affero General Public License v3](LICENSE). Non è fornita alcuna garanzia.

È realizzato con [JUCE](https://juce.com) (usato con l'opzione AGPLv3),
[Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch),
[ONNX Runtime](https://onnxruntime.ai), il modello [Demucs](https://github.com/facebookresearch/demucs) di Meta, le forme
delle icone di [Lucide](https://lucide.dev), i font Outfit e DM Sans e i campioni
[Big Rusty Drums](https://github.com/sfzinstruments/karoryfer.big-rusty-drums) di Karoryfer Samples.
L'elenco completo con le licenze è in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) e nell'app, in Informazioni >
Licenze open source.

Practice Vibes è fatto da un chitarrista, Camillo Patricelli, con il *vibe coding*: l'idea, il design, le decisioni e
i test sono suoi; il codice è scritto da [Claude](https://www.anthropic.com/claude) (l'IA di Anthropic) tramite
[Claude Code](https://claude.com/claude-code). Il nome è un omaggio a questo modo di lavorare.

Il nome "Practice Vibes" e l'icona dell'app appartengono all'autore. La licenza copre il codice, non il nome: se
pubblichi una versione modificata, per favore dalle un nome e un'icona diversi.

Contatti: [practicevibes.app@gmail.com](mailto:practicevibes.app@gmail.com)
