docker build -t everest-charge-som-cross -f docker/Dockerfile --build-arg USER_NAME=$(id -u -n) --build-arg USER_ID=$(id -u) --build-arg GROUP_ID=$(id -g) .
docker run -it --rm --volume <path/to/workspace>/everest-ui:/everest-ui --volume <path/to/sysroot>:/sysroot:ro --volume <path/to/tooling>/tooling:/tooling:ro docker.io/library/everest-charge-som-cross
