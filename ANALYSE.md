# QDSSD — Messbericht der Exporte

Projekt: `90-Under-One-Sun` · 19,46 s · 44,1 kHz · 24 Bit · stereo
Quelle: MPC-Exporte, Ordner `MPC Documents/Projects` und SSD.

## 1. Erste Messung: Full-Mix Dry/Fx (08:55/08:56)

Zu diesem Zeitpunkt stand **Depth 2,1 dB** — die Voreinstellung war noch nicht
auf den Arbeitspunkt gesetzt.

```
Dry  rms −7,42 dBFS   Fx rms −7,54 dBFS   → 0,12 dB leiser
Diff rms −26,4 dBFS   Delay 0 Samples
250 Hz: −1,71 dB, alle anderen Bänder < 1 dB
```

Ergebnis: korrekt, aber bedeutungslos leise. Kein Fehler im DSP.

## 2. Zweite Messung: Non-mute und Non-mute-Delta (09:15/09:16)

```
normal (wet mix)   rms −7,54 dBFS   peak −0,09
delta (entfernt)   rms −13,98 dBFS  peak −0,09
```

Bandverteilung im Delta (relativ zum Mix):

| Band | Delta | Normal | Einordnung |
|---|---|---|---|
| 31 Hz | 34,8 | 49,6 | −15 dB → Sub bleibt |
| 63 Hz | 30,4 | 38,8 | −8 dB → Kick kaum betroffen |
| 125 Hz | 37,7 | 43,9 | −6 dB |
| 250 Hz | 35,0 | 34,9 | praktisch vollständig entfernt |
| 500 Hz | 42,1 | 40,3 | vollständig entfernt |
| 1–8 kHz | 33–37 | 28–34 | vollständig entfernt |
| 16 kHz | 9,3 | 7,1 | vollständig entfernt |

Das Delta folgt dem Gesang (Hüllkurve −34,5 … −7,0 dBFS). Das ist das
gewünschte Verhalten: Sub geschützt, Vocal-Bänder kräftig freigestellt.

## 3. Dritte Messung: Vocal-mute (Beat solo, 09:14)

```
Vocal-mute (nasser Beat)  rms −7,28 dBFS  peak −0,09
Delta                     rms −13,98 dBFS peak −0,09
```

`mute + 0,3 × delta` peakt bei +2,2 dBFS, obwohl die Summe rechnerisch der
trockene Beat sein müsste. Das ist ohne weitere Annahme unmöglich.

## 4. Warum die absoluten Pegel nicht vergleichbar sind

**Alle alten Exporte peaken exakt bei −0,09 dBFS.** Das war die Decke der
damaligen Master-Kette (Maximizer), nicht der Export: Die MPC normalisiert
nicht (vom Nutzer bestätigt). Ein Delta-Signal würde ohne diese Kette
deutlich leiser sein als der Mix; die absoluten Pegel bleiben deshalb
unbrauchbar.

Konsequenz:

- **Innerhalb** einer Datei sind die Bandverhältnisse belastbar.
- **Zwischen** Dateien sind Pegelvergleiche und die Rekonstruktion
  `trocken = nass + delta` nicht belastbar.

## 5. Was daraus folgt

1. Der DSP arbeitet korrekt und selektiv: Sub frei, ab 250 Hz starke Absenkung.
2. Die exakte dB-Zahl pro Band braucht **Exporte ohne Master-Kette**.
3. Für die nächste Messung: Maximizer und Master-Plugins umgehen, dann
   Beat-Solo mit und ohne QDSSD exportieren.

## 6. Reproduzierbare Prüfungen im Repo

```
mpc/tools/spec-test.cpp    STFT-Engine: Unity, Latenz, Ducking
mpc/tools/duck-test.cpp    Filterbank: exakte Rekonstruktion, volles Ducking
mpc/tools/bus-test.cpp     Shared Memory: Seqlock, Zeitstempel, 8 Busse
mpc/tools/chain-test.cpp   Analyse → Maske → Filterbank, alle drei Profile
```

`chain-test` (Sinus, 24 dB):

```
Gentle   220 Hz −2,91 dB   60 Hz −0,23 dB
Strong   220 Hz −5,99 dB   60 Hz −0,99 dB
Extreme  220 Hz −6,33 dB   60 Hz −1,34 dB
```

Auf echten Vocals greifen mehr Bänder, deshalb ist der hörbare Unterschied
zwischen den Profilen größer als diese Einzelton-Messung.
