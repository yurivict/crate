# crate

Crate is a containerizer for FreeBSD. It can containerize any packages and/or services into the container file, and then run the container at a later time.

Crate contailers contain everything that is needed to run the containerized software, and only need the crate executable and a system functions from the kernel to run.

## Features
* networking: both outgoing and incoming can be enabled for the crate, so that TCP and UDP connections can be initiated and accepted by the program running in the crate
* video: programs can play videos using /dev/videoN devices
* shared folders and files: host folders and files can be shared

## Documentation
The manpage exists but it doesn't explain much yet, it has to be expanded.

## Examples
```examples/``` subdirectory contains a number of working examples.

## Crate status
```crate``` is in its alpha stage. Some features might not work as inended. I only started wlorking on it in the late June, 2019, so it is a very new project.

## Workflow
The workflow is based on two simple operations that crate supports: ```create``` and ```run```.
* ```create``` the crate container with the command: ```crate crate-spec.yml``` (this is done once for a crate)
* ```run``` the crate with the command: ```crate my-crate.crate``` (this can be done multiple tines for a crate)

## How to install
crate can be installed using the port ```sysutils/crate```: cd /usr/ports/sysutils/crate && make install clean```
Unfortunately, due to some STL features (```filesystem```) not being available in the version of the system 12.0-RELEASE-p7 used by the poudriere(8), packages can't be available on FreeBSD 12, but it can be built and installed manually with the above command. Packages should be available on FreeBSD 13.

