# gr-ais
Automatic Information System decoder for shipborne position reporting for the Gnuradio project

## What is it?
gr-ais allows you to use any number of software-defined radio receivers to receive [AIS](https://en.wikipedia.org/wiki/Automatic_identification_system) transmissions from ships and shore stations. The output can be routed to any number of programs for translation and display: [OpenCPN](https://opencpn.org/), [gpsd](https://gpsd.gitlab.io/gpsd/), and many others.

## How does it work?
AIS uses GMSK transmissions at 161.975MHz and 162.025MHz to send location data. An SDR such as a [USRP](https://ettus.com/), [RTL-SDR](https://www.rtl-sdr.com/), [AirSpy](https://airspy.com/), or [HackRF](https://greatscottgadgets.com/hackrf/) digitizes both channels at once, sending the baseband data to gr-ais using Gnuradio and [SoapySDR](https://github.com/pothosware/SoapySDR/wiki). gr-ais translates the baseband RF data to AIS packets using the magic of math. If you want to know more, use GNURadio Companion to browse the flowgraphs in gr-ais/apps.

## How do I use it?
You will need to install Gnuradio 3.8 or 3.9, a hardware driver for your SDR (UHD, OsmoSDR, etc.), SoapySDR, and gr-soapy. In the near future GNURadio will be incorporating gr-soapy, and so you will be able to just install Gnuradio, SoapySDR, and a SDR driver, with no other prerequisites.

One caveat is that I had to fix a bug in the GMSK timing error detector in GNURadio, and so for gr-ais to work well, you will need a version of Gnuradio which includes this fix. Because I just got around to fixing it today (9 Mar 21), you'll want a version which was compiled after that date. 

Once you have the prerequisites, you can install it like any other Gnuradio project:

$ mkdir build
$ cd build
$ cmake ../
$ make
$ sudo make install

Once that's done, you can run ais_rx.py:

nick@prawn:~/dev/gr-ais/apps grc*$ ./ais_rx.py --help
usage: ais_rx.py [-h] [--ant ANT] [--args ARGS] [--dev-str DEV_STR] [--gain GAIN]
                 [--samp-rate SAMP_RATE] [--stream-args STREAM_ARGS] [--ted-bw TED_BW]
                 [--threshold THRESHOLD]

optional arguments:
  -h, --help            show this help message and exit
  --ant ANT             Set Antenna [default='TX/RX']
  --args ARGS           Set Device args [default='']
  --dev-str DEV_STR     Set Device string [default='device=uhd']
  --gain GAIN           Set Gain [default=65]
  --samp-rate SAMP_RATE
                        Set Sample rate [default='200.0k']
  --stream-args STREAM_ARGS
                        Set Stream args [default='']
  --ted-bw TED_BW       Set TED bandwidth [default='33.0m']
  --threshold THRESHOLD
                        Set Correlator threshold [default='830.0m']

You will have to change these settings based on what SDR you are using. An RTL-SDR can only sample at 2.4Msps, for instance. Antenna names will vary between devices. You can probably leave --ted-bw and --threshold alone: the defaults should be fine. The sample rate just needs to be above 70kHz, so gr-ais can capture both AIS channels for decoding. If you wish to use a much slower device (a soundcard on a discriminator tap, for instance) you could create a version which uses an Audio Source block and which feeds only a single AIS receiver core block.
