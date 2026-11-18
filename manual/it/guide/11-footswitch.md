# 11. Footswitch

Un **footswitch MIDI** ti permette di comandare il looper, registrare take e altro senza togliere le mani dallo
strumento. Va bene qualsiasi pedale che manda MIDI, con un cavo USB o via **Bluetooth MIDI**.

---

## Collegare il pedale

- **USB**: collegalo. Compare da solo.
- **Bluetooth MIDI**:
  - **iPad / Mac**: tocca il pulsante **Bluetooth** nella barra in alto, scegli il tuo pedale nell'elenco e collegalo.
    (iPad: l'elenco si apre in un pannello dell'app; **Chiudi** quando hai finito.)
    (Mac: in alternativa usa Configurazione MIDI Audio › Finestra › Mostra MIDI Studio › Bluetooth.)
  - **Windows**: prima abbina il pedale in Impostazioni di Windows › Bluetooth e dispositivi. Il pulsante **Bluetooth**
    nella barra in alto lo spiega e ha un pulsante **Apri le impostazioni Bluetooth** che ti porta direttamente lì.

Per controllare che funzioni: scheda **Rig** › **Footswitch MIDI** mostra l'ultimo messaggio MIDI ricevuto. Premi uno
switch e dovrebbe cambiare.

---

## Configurarlo

Apri **Configura footswitch...** (scheda Rig, o **Footswitch** nella scheda Looper).

1. **Apprendi A-D in sequenza**, poi premi gli switch del pedale uno dopo l'altro, nell'ordine in cui vuoi che siano
   A, B, C, D. (Oppure **Apprendi** un singolo switch.) Una spia si accende quando l'app vede una pressione.
   **Dimentica** cancella uno switch appreso.
2. Per ogni switch, scegli un'azione per il **Tocco** e, se vuoi, una per **Tieni 1 s**.

Predefinito: switch A–D = tracce del looper 1–4. Il **tocco** registra, sovraincide o suona la traccia; **tenuto** la
svuota.

Le azioni:
- Traccia 1–4: registra / sovraincidi / suona · svuota · muto / attiva
- Annulla l'ultimo overdub · Muto / attiva tutte le tracce · Svuota tutte le tracce
- Avvia / ferma il metronomo
- Accordatore on / off
- Registra un take (avvia / ferma)
- Muto / attiva uno degli stem del brano (voce, chitarra, basso, batteria, piano o altro: ognuno ha la sua voce)
- Rig: bypass del plugin selezionato

L'azione del tocco avviene **alla pressione**, così un loop parte esattamente quando pesti il pedale. Le righe del
looper mostrano quale switch le comanda ("pedale A").

Gli switch appresi vengono ricordati, e il pedale funziona di nuovo la volta dopo senza doverlo riapprendere.

---

## Tocco e tenuto richiedono un pedale "momentaneo"

Per distinguere un tocco da una pressione tenuta, l'app deve sapere quando premi **e** quando rilasci lo switch. I
pedali possono funzionare in due modi:
- **Momentaneo**: un messaggio alla pressione, un altro al rilascio. Tocco e tenuto funzionano entrambi.
- **Toggle / a ritenuta**: un messaggio per ogni pestata. Funziona solo l'azione del tocco, e l'app mostra una nota al
  riguardo.

Quasi tutti i pedali si possono passare in modalità momentanea dalle loro impostazioni o dalla loro app. Dopo, fai
apprendere di nuovo gli switch.

> **Esempio – M-VAVE Chocolate:** nella sua app CubeSuite scegli **Custom Control**, dai a ogni switch un suo numero CC
> e imposta **Visual behavior** su **Off** (momentaneo). Poi fai apprendere gli switch in Practice Vibes.

Avanti: **[12. Risoluzione dei problemi](12-troubleshooting.md)**
