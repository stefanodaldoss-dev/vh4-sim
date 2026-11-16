# VH4 Sim (versione 0.1)

Simulatore di testata a valvole (4 canali, power amp, cabina) per chitarra. App standalone per Windows (Intel x64 e ARM64).

## Cartelle
- `dsp/vh4dsp.hpp`  il motore audio (preamp, power amp, cabina)
- `app/`            l'applicazione (finestra, manopole, scelta scheda audio)
- `tools/`          strumenti da riga di comando (render offline, misure di velocita', prove)
- `.github/workflows/build.yml`  ricetta che fa compilare il programma a GitHub

## Come ottenere il programma (senza installare nulla sul PC)
1. Crea un account gratuito su github.com e un repository **pubblico** nuovo (per esempio `vh4-sim`).
   Il repository pubblico serve perche' la compilazione per il PC Snapdragon (ARM64) e' gratuita solo cosi'.
2. Decomprimi `vh4-sim.zip` sul PC. Nel repository: "Add file" > "Upload files", trascina TUTTO il contenuto
   della cartella (comprese `.github`, `app`, `dsp`, `tools`, `CMakeLists.txt`) e premi "Commit changes".
3. Apri la scheda "Actions". Parte da solo il lavoro "build" (10-15 minuti la prima volta).
4. A lavoro finito (spunta verde) apri l'esecuzione e in fondo, sotto "Artifacts", scarica
   `VH4Sim-x64` (PC Intel) e/o `VH4Sim-ARM64` (PC Snapdragon).
5. Decomprimi e avvia `VH4 Sim.exe`. Se Windows avvisa "SmartScreen": "Ulteriori informazioni" > "Esegui comunque".

## Primo avvio
1. "Impostazioni audio": scegli il tipo di dispositivo e la scheda. Prova nell'ordine "Windows Audio (Exclusive Mode)" o "Windows Audio (Low Latency Mode)",
   poi riduci il buffer finche' il suono non scatta. La latenza stimata appare in basso.
2. Ingresso: chitarra sull'ingresso 1. Parti con INPUT al centro e MASTER a 5, volume basso.
3. Le impostazioni si salvano da sole.

## Novita' della versione 0.2
- Livelli: il canale 1 (pulito) e' ora compensato (+10 dB) e il suo preset parte da gain 8 / volume 8. Prima con gain 3 usciva 35-65 dB piu' piano degli altri.
- GATE: manopola nella fila in basso (0 = spento). Taglia il soffio della scheda quando non suoni. Parti da circa -70 dB e alzalo finche' il fruscio sparisce nelle pause.
- Indicatori in dB con picco: i colpi forti sulla chitarra dovrebbero stare a circa -12 dB all'ingresso.
- Preset: Pulito, Crunch, Lead, Lead 2 e due "stile Jones" (da impostazioni lette in forum, non verificate).
- Cabina da file IR: pulsante "Carica IR cabina...". Serve un file .wav di risposta all'impulso di una cassa (es. 4x12 con V30).
  Una raccolta gratuita e' la "IR Cab Library" di Origin Effects (origineffects.com/product/ir-cab-library/): controlla la licenza nella pagina di download.
  Cerca ad esempio i file della "4x12 Bogner V30" o della "Marshall 1960B".
