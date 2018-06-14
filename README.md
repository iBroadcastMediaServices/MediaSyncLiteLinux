iBroadcast Media Sync Lite v0.4.2

- gcc
- make
- gtk+3.0 development files (usually libgtk3-dev) >= 3.10
    - on Ubuntu/Debian, use libgtk-3-dev
    - on Fedora, use gtk3-devel
- libcurl
    - on Ubuntu, use libcurl4-openssl-dev
    - on Fedora, use libcurl-devel
- openssl
- libssl-dev
- libjansson (available in ./jansson-2.7)
    - on Ubuntu/Debian, use libjansson-dev

Just execute:

- make

You can then run MediaSync Lite from the directory which it was built. 

Optionally, if you would like to install so MediaSync Lite is available system wide (as root):

- make install

or to install as regular user:

- sudo make install

Application will be installed in /usr/local/bin directory.
