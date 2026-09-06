# Ubuntu 24.04 development container

Install Docker and the VS Code Dev Containers extension, open this repository,
and run **Dev Containers: Reopen in Container**. The image includes Ubuntu's
GNU Radio 3.10 runtime and development packages, C++ and Python build tools,
GDB, and Doxygen. Initial setup configures CMake in `build/devcontainer`.

Run these commands in the container terminal:

```sh
cmake --build build/devcontainer --parallel 2
ctest --test-dir build/devcontainer --output-on-failure
sudo cmake --install build/devcontainer
sudo ldconfig
python3 -c 'from gnuradio import ais; print(ais.invert)'
ais_rx --help
```

Rebuild and reinstall after changes when running the installed `ais_rx` app.
CTest uses the build tree, so installation is not required to run tests.
The separate build directory avoids reusing a host CMake cache.
The repository's `.gitattributes` preserves LF line endings on Windows, which
keeps executable scripts and GNU Radio's binding header hashes valid.

The default setup supports builds, tests, and processing recorded samples.
Live SDR use requires the appropriate SoapySDR driver and host-specific device
forwarding. GNU Radio Companion also requires a graphical display connection.
