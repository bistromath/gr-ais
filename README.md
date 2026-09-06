# gr-ais

An AIS receiver for GNU Radio 3.10. Decodes GMSK transmissions into NMEA
`!AIVDM` sentences using coherent Viterbi demodulation, NRZI decoding, and
HDLC deframing with CRC checks.

## Build and test

The [Ubuntu 24.04 devcontainer](.devcontainer/README.md) includes the build
dependencies and development tools. Open the repository in VS Code and select
**Dev Containers: Reopen in Container**, then follow its build instructions.

For a native Ubuntu 24.04 build:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build gnuradio gnuradio-dev \
    pybind11-dev python3-dev python3-numpy python3-packaging python3-pygccxml castxml
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local -DPYTHON_EXECUTABLE=/usr/bin/python3
cmake --build build/local --parallel 2
ctest --test-dir build/local --output-on-failure
sudo cmake --install build/local
sudo ldconfig
```

## Receive

Live reception requires GNU Radio's SoapySDR support and a driver for your SDR.
For example, with an RTL-SDR:

```sh
ais_rx --args="driver=rtlsdr" --rate=250e3
```

The receiver tunes to 162 MHz and decodes both AIS channels. Use `--singlechannel`
to tune to channel A at 161.975 MHz. Decoded sentences are printed to stdout.

Files and UDP streams accept raw interleaved float32 I/Q samples. Match `--rate`
to the recording or stream; use samples centered at 162 MHz for both channels,
or at 161.975 MHz with `--singlechannel`.

```sh
ais_rx --source=capture.cf32 --rate=250e3
ais_rx --source=udp:1234 --rate=250e3
ais_rx --help
```

## Python blocks

Import blocks with `from gnuradio import ais`. The CLI uses the streaming
`ais_demod` receiver. `ais_burst_demod(samples_per_symbol)` provides a separate
data-aided receiver for isolated bursts at an integer number of samples per
symbol. Its default permits adjacent single-slot bursts; set `burst_slots` for
captures with known longer bursts. Frame length is not detected automatically.

License: GPL-3.0-or-later.
