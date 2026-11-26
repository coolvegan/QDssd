# QDSSD — Übergabe

**Dynamic Spectral Sidechain Ducking** · Vendor: r3d0xStyle
Stand: 2026-11-25 · Version 1.1.2 (MPC) · Ziel: Akai MPC OS 3.10.1+

---

## 1. Was das Plugin tut

QDSSD nimmt dem Beat Energie weg — aber nur dort, wo die Stimme gerade
Energie hat. Es ist kein Kompressor, der das ganze Signal absenkt, sondern
eine **spektrale Freistellung** in 16 Frequenzbändern.

Beispiel: Singt die Stimme bei 220 Hz, wird der Beat im Band um 220 Hz
abgesenkt. Der Kick bei 50 Hz bleibt unangetastet. Dadurch bleibt der Groove
stehen, während die Stimme Platz bekommt.

Die Kette besteht aus **zwei Plugins**, weil die MPC einem Effekt nur die
zwei Eingänge seiner eigenen Spur gibt — es gibt keinen Host-Sidechain-Bus.

```
Vocal-Spur:   [QDSSD SC Input]  ──►  Shared-Memory-Bus 1–8
                     │                        │
              Audio unverändert              │  Chroma, Noten, Spektrum
                     ▼                        ▼
                  Mixer                 [QDSSD]  ◄── Maske pro Band
                                              │
                                        gefilterter Beat
```

## 2. Signalweg im Detail

**SC Input (Sender, auf der Vocal-Spur)**

1. STFT mit 1024 Punkten, Hop 256 (75 % Overlap), sqrt-Hann-Fenster
2. Chroma (12), Notenwahrscheinlichkeiten (128), 128-Band-Spektrum
3. Ein Frame pro Hop in den Bus, mit monotonem Zeitstempel
4. Audio wird unverändert durchgereicht

**QDSSD (Empfänger, auf der Beat-Spur)**

1. 16 logarithmische Bänder, 60 Hz – 16 kHz
2. Jeder Split ist ein Butterworth-Tiefpass 2. Ordnung mit `HP = x − LP`.
   Dadurch summieren sich die Bänder bei Gain 1 **exakt** zum Eingang.
3. Key-Maske → Bandgewichte → Gain pro Band, geglättet mit Attack/Release
4. **0 Samples Latenz** — es sind Filter, keine FFT im Audioweg

## 3. Bus

```
/dev/shm/qdssd_sc_buses_v1
8 Busse · je 64 Frames · Seqlock · monotone Zeitstempel
Frame: seq, flags, timeNs, level, chroma[12], notes[128], spectrum[128]
```

Der Empfänger liest den neuesten gültigen Frame. Der Bus wird **lazy** im
`process()` geöffnet, damit die Ladereihenfolge der beiden Plugins egal ist.
Ohne Sender bleibt der Empfänger transparent.

## 4. Bedienung

| Regler | Bedeutung |
|---|---|
| **Bus** | 1–8. Muss bei Sender (`To Bus`) und Empfänger (`Bus`) gleich sein. **Nicht** die MPC-Submixe. |
| **Depth** | Maximale Absenkung, 0–24 dB. 24 dB ist der Arbeitspunkt. |
| **Speed** | Attack/Release zusammen: links schnell/trocken, rechts weich/saugend |
| **Mix** | Dry/Wet |
| **Delta** | Hört nur, was weggenommen wird — das wichtigste Diagnosewerkzeug |
| **Intensity** | Gentle · Strong · Extreme — Form und Grundzug der Maske |

Die Profile:

| Profil | Depth-Skalierung | Breitband | Wirkung bei Depth 24 |
|---|---|---|---|
| Gentle | 0,50 | 15 % | ~−1 dB Grundzug, nur klare Noten |
| Strong | 0,80 | 40 % | ~−4 dB Grundzug |
| Extreme | 1,00 | 70 % | ~−9 dB Grundzug, breite Freistellung |

Der **Sub-Schutz** (Band 0/1 auf 30 %) gilt in allen Profilen.

## 5. Messungen (Auszug)

Analysiert wurden Exporte des Projekts `90-Under-One-Sun`:

- **kein Versatz**: 0 Samples, Korrelation 0,994
- **keine Klicks**, kein Clipping
- **Sub geschützt**: im Delta −15 dB relativ zum Mix bei 31 Hz
- **ab 250 Hz**: Delta auf Mix-Niveau, also praktisch vollständig entfernt

Wichtige Einschränkung: **Die alten Exporte wurden von der damaligen
Master-Kette bei −0,09 dBFS begrenzt**, nicht vom Export — die MPC normalisiert
nicht. Absolute Pegelvergleiche zwischen diesen Dateien sind deshalb nicht
belastbar. Die Bandverteilung innerhalb einer Datei ist es sehr wohl. Details in [`ANALYSE.md`](ANALYSE.md).

## 6. Bekannte Grenzen

1. **Notenerkennung ist grob.** Chroma-Akkumulation plus Harmonik-Kamm, ohne
   polyphone Analyse. Bei hauchigen oder mehrstimmigen Vocals wird die Maske
   unscharf. Deshalb ist sie weich und der Hybrid-Modus Standard.
2. **Alte Exporte master-limitiert.** Für exakte dB-Werte ohne Master-Kette
   exportieren; die MPC selbst normalisiert nicht.
3. **Kein Desktop-Build.** Im DAW wäre ein echtes 4-Eingangs-Plugin möglich;
   derzeit gibt es nur das MPC-Paar.
4. **Plugin-State der MPC ist undurchsichtig.** Die `.xpj` speichert den
   VST-Chunk in einem eigenen Binärformat (`260.CMlaKA…`). Offline lassen sich
   Parameter deshalb nicht setzen; Bypass über das Projekt-JSON funktioniert.
5. **Keine Presets, kein Installer-Skript für Endnutzer.** Das ZIP enthält
   beide Ordner und eine INSTALL.txt.

## 7. Nächste sinnvolle Schritte

1. **Unnormalisierten Export** besorgen → exakte Absenkung pro Band messen.
2. **Notenanalyse verbessern**: Hysterese über mehrere Frames, Grundton per
   YIN/Autokorrelation statt nur HPS.
3. **Desktop-Build** (JUCE/DPF) mit 4 Eingängen und echtem Spektrum-Analyzer.
4. **Presets** (z. B. „Vocal Clear", „Dub Duck", „Gentle Room") und README.

## 8. Dateien

```
src/dsp/FilterDucker.hpp   16-Band-Filterbank, 0 Latenz
src/dsp/KeyMask.hpp        Noten/Spektrum → Bandgewichte, Intensitätsprofile
src/dsp/NoteAnalyzer.hpp   STFT-Analyse des Vocals
src/dsp/Bus.hpp            Shared-Memory-Bus mit Seqlock
src/dsp/SpectralEngine.hpp STFT-Resonanz-Engine (derzeit ungenutzt)
src/QDssdMpc.cpp           Empfänger-Plugin
src/QDssdScInput.cpp       Sender-Plugin
mpc/                       Skins, Metadaten, Tools, Framework
scripts/build-mpc.sh       Build + Test + Cross-Compile + ZIP
scripts/install-mpc.sh     Installation auf der MPC
concept0..2                Gemini-Entwürfe (Referenz)
Grafik*.webp               Konzeptgrafiken
Roastme.txt                Design-Review-Anfrage
ANALYSE.md                 Messbericht der Exporte
```

## 9. Build und Installation

```bash
cd ~/Dev/QDssd
./scripts/build-mpc.sh      # Host-Tests, Skins, ARM32/ARM64, ZIP
./scripts/install-mpc.sh    # beide Ordner nach VST2 Plugins/
```

Getestet: `spec-test`, `bus-test`, `duck-test`, `chain-test`, Host-Simulator
(19/19 und 20/20), Skin-Validator (0 Fehler).
