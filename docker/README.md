To build a docker container "everest-charge-som-cross" from this Dockerfile, use:

```bash
$ docker build -t everest-charge-som-cross -f docker/Dockerfile --build-arg USER_NAME=$(id -u -n) --build-arg USER_ID=$(id -u) --build-arg GROUP_ID=$(id -g) .
```

To run this docker container, provide a Charge SOM Developer image
sysroot, the everest-ui git source dir, and a directory with chargebyte
toolchain file as `EVerest_toolchain/toolchain.cmake`:
```
docker run -it --rm --volume <path/to/workspace>/everest-ui:/everest-ui --volume <path/to/sysroot>:/sysroot:ro --volume <path/to/tooling>/tooling:/tooling:ro docker.io/library/everest-charge-som-cross
```

Inside the running container, change directory to /everest-ui and run
`cmake` and `make` there. (Example commands can be found in the bash
history. Adapt paths if necessary.)

After compilation with `make install DESTDIR=install ...`, you can find the
resulting directory structure under your everest-ui git checkout's
`build*/install` directory.
