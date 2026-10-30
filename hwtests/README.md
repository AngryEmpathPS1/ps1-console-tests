# The tests

Each folder is one test: `main.cpp`, its `Makefile`, and sometimes a `gen.js` that wrote the test's code, or a `PREDICTED.txt` with what was expected before it first ran on a console. `build/` holds the built programs, one PS-EXE per test. `runner/` is not a test: it is the resident program most logs were taken under, and its header gives its protocol.

## Building

The tests build with the Docker toolchain of [ps1-tests](https://github.com/JaCzekanski/ps1-tests), and they link its `common` library. Clone it beside this repository, or point `PS1_TESTS` at it, and have Docker running:

```
node hwtests/build.js              # every test
node hwtests/build.js store-cost   # just one
node hwtests/build.js --clean      # throw the build folder away first
```

Nothing is installed on the host except the Docker image `jaczekanski/psn00bsdk` (247 MB). The build folder must have no spaces in its path: set `PSX_HWBUILD` if your temporary folder has one.
