# crate

Crate is a containerizer for FreeBSD. It can containerize any packages and/or services into a container file, and then run the container at a later time.

Crate containers contain everything that is needed to run the containerized software, and only need the crate executable and system functions from the kernel to run.

## Features
* networking: both outgoing and incoming connections can be enabled for the crate, so that TCP and UDP connections can be initiated and accepted by the program running in the container
* video: programs can play videos using /dev/videoN devices
* shared folders and files: host folders and files can be shared with crates, allowing them to run using persistent state

## Documentation
The manpage exists but it doesn't explain much yet, it has to be expanded.

## Examples
```examples/``` subdirectory contains a number of examples.
Working examples (only the examples listed here are verified to be working, other examples probably do not work):
* firefox.yml: you can run the firefox browser. It will run as if it was started for the first time. It will not preserve cookies or user settings across runs. You can simultaneously run any number of instances of the firefox browser, they all will run completely independently.
* chromium.yml: similarly to firefox.yml, you can create the chromium crate (chromium.crate) which will not preserve cookies, etc., just like firefox
* opera.yml: opera.crate runs, but many sites appear broken, unlike with firefox.crate and chrome.crate
* kodi.yml: kodi.crate runs but the texts aren't displayed due to missing font dependencies
* gogs.yml: runs fine, listens on the port 3100, uses the persistent database
* amass.yml: runs fine
* qtox.yml: runs fine in the crate, at the moment doesn't have a persistent state
* xeyes.yml: runs fine

## Crate status
```crate``` is in its alpha stage. Some features might not work as intended. I only started working on it in the late June 2019, so it is a very new project.

## Workflow
The workflow is based on two basic operations that crate supports: ```create``` and ```run```.
* ```create``` the crate container with the command: ```crate crate-spec.yml``` (this is done once for a crate)
* ```run``` the crate with the command: ```crate my-crate.crate``` (this can be done multiple times for a crate)

## How to install
crate can be installed using the port ```sysutils/crate```: cd /usr/ports/sysutils/crate && make install clean```

Unfortunately, due to the STL feature ```filesystem``` not being available in the version of the system 12.0-RELEASE-p7 used by the poudriere(8), packages can't be available on FreeBSD 12, but it can be built and installed manually with the above command. Packages should be available on FreeBSD 13.

