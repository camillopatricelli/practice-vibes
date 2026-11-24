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
svuota. Tenendo premuto **D** passi alla modalità scene (vedi sotto), quindi la traccia 4 si svuota sullo schermo o con
**Svuota tutte le tracce**.

Le azioni:
- Traccia 1–4: registra / sovraincidi / suona · svuota · muto / attiva
- Annulla l'ultimo overdub · Muto / attiva tutte le tracce · Svuota tutte le tracce
- Avvia / ferma il metronomo
- Accordatore on / off
- Registra un take (avvia / ferma)
- Muto / attiva uno degli stem del brano (voce, chitarra, basso, batteria, piano o altro: ognuno ha la sua voce)
- Rig: bypass del plugin selezionato
- Rig: scena 1–4 · Rig: scena successiva · Rig: scena precedente (capitolo 10)
- Cambia modalità del footswitch (looper / scene)

L'azione del tocco avviene **alla pressione**, così un loop parte esattamente quando pesti il pedale. Le righe del
looper mostrano quale switch le comanda ("pedale A").

Gli switch appresi vengono ricordati, e il pedale funziona di nuovo la volta dopo senza doverlo riapprendere.

---

## Due modalità: looper e scene

Quattro switch non bastano per tutto. Per questo il pedale ha due modalità, ognuna con le sue azioni per il tocco e
per la pressione tenuta:
- **Modalità looper** (all'avvio): A–D comandano le tracce del looper 1–4, come sopra.
- **Modalità scene**: A–D passano alle scene 1–4 del rig (capitolo 10), così vai dal pulito al crunch con il piede.

**Tieni premuto D** per cambiare modalità. In modalità scene i loop continuano a suonare; solo non li comandi con i
piedi finché non torni indietro.

La barra in alto mostra la scena attuale e si illumina in modalità scene. Puoi cambiare modalità anche lì (toccala), o
in cima a **Configura footswitch...**, dove **Modalità looper** / **Modalità scene** sceglie anche di quale modalità
stai modificando le azioni.

> **Pedale a ritenuta?** Non può tenere premuto, quindi la modalità cambia con un **tocco su D**. La modalità looper ha
> allora le tracce 1–3 e la modalità scene le scene 1–3.

Puoi cambiare ogni azione in entrambe le modalità. Lascia solo **Cambia modalità del footswitch** su uno switch in
ciascuna modalità, altrimenti non torni indietro con i piedi. **Azioni predefinite** rimette la disposizione descritta
sopra.

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
