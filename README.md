# AI Workspace - Milestone 0 v1.1

This repository is the M0 starter project.

## Docker (M0: console only)

```bash
docker build -t ref-env .
docker run -it --rm -v "$PWD":/mnt -w /mnt ref-env
```

Inside the container:

```bash
mkdir build
cd build
cmake ..
make
ctest --output-on-failure
./main
```

The starter repository is intentionally **compilable but functionally incomplete**. The starter tests are only smoke tests and are expected to fail until you implement the required behavior.

Read the M0 specification before implementing the classes.
