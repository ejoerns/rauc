[![GitHub license](https://img.shields.io/badge/license-LGPLv2.1-blue.svg)](https://raw.githubusercontent.com/jluebbe/rauc/master/COPYING) [![Travis branch](https://img.shields.io/travis/jluebbe/rauc/master.svg)](https://travis-ci.org/jluebbe/rauc) [![Coveralls branch](https://img.shields.io/coveralls/jluebbe/rauc/master.svg)](https://coveralls.io/r/jluebbe/rauc) [![Coverity](https://img.shields.io/coverity/scan/5085.svg)](https://scan.coverity.com/projects/5085)

# rauc -- Robust Auto-Update Controller

> rauc controls the update process on embedded linux systems

## Features

* Update slot selection logic
* custom install handlers
* signed bundles (X509 infrastructure)
* Supported bootloaders: barebox, grub
* d-bus API

## Host features

* Create update bundles
* Show bundle info
* Sign / resign bundles

## Target features

* Run as a service
* Install bundles
* Proivde slot status information

## Contributing

Fork us, try it out.
Open an pull request or an issue.

## Debugging

To debug rauc, run it with `G_MESSAGES_DEBUG` set to `all`:

    G_MESSAGES_DEBUG=all ./rauc status
