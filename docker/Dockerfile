# build with docker:
# docker build -t everest-charge-som-cross -f <path to this Dockerfile> --build-arg USER_NAME=$(id -u -n) --build-arg USER_ID=$(id -u) --build-arg GROUP_ID=$(id -g) .

# Important: make sure the python version of the system matches the python version
# of the target rootfs
# For yocto kirkstone this is python 3.10, which is the default version in ubuntu 22.04 jammy
FROM ubuntu:jammy AS base
ENV DEBIAN_FRONTEND=noninteractive
ENV container=docker

ARG USER_NAME
ARG USER_ID
ARG GROUP_ID

RUN apt-get -y update
# Install essential build stuff
RUN apt-get -y install wget sudo nano curl file rsync locales \
    build-essential git cmake clang-tidy

FROM base AS crosstools

# Install cross-build stuff
RUN apt-get -y install \
    libc6-arm64-cross binutils-aarch64-linux-gnu gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    qtchooser qtbase5-dev qttools5-dev-tools # for qmake, moc, rcc

RUN echo "export LANG=en_US.UTF-8" >> /etc/profile.d/set-locale.sh
RUN echo "export LANGUAGE=en_US" >> /etc/profile.d/set-locale.sh
RUN sed -i -e 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
RUN locale-gen en_US.UTF-8

RUN apt-get -y clean
RUN rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

RUN echo "alias ll='ls -la'" >> /etc/bash.bashrc
FROM crosstools AS user

RUN echo "Creating user ${USER_NAME} with user ID: ${USER_ID} and group ID: ${GROUP_ID}"
#RUN deluser ubuntu # Note: for Ubuntu >= 24.04 we need to remove the default ubuntu user with the default 1000 user ID
RUN addgroup --gid $GROUP_ID $USER_NAME
RUN adduser --uid ${USER_ID} --gid ${GROUP_ID} --disabled-password --gecos "" ${USER_NAME}
RUN echo "${USER_NAME} ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

RUN echo "export PATH=$PATH:/home/${USER_NAME}/.local/bin" >> /home/${USER_NAME}/.bash_profile
RUN echo "export CPM_SOURCE_CACHE=/home/${USER_NAME}/.cache/CPM" >> /home/${USER_NAME}/.bash_profile
# mount with
# docker run -it --rm --volume ~/Development/EVerest/everest-workspace/everest-ui:/everest-ui --volume /mnt2:/sysroot:ro --volume ~/Development/bitbucket/EVerest/tooling:/tooling:ro
RUN echo 'cmake -B build-cross-docker -DCMAKE_TOOLCHAIN_FILE=/tooling/EVerest_toolchain/toolchain.cmake -DCMAKE_SYSROOT=/sysroot -DOE_QMAKE_PATH_EXTERNAL_HOST_BINS:STRING="/usr/bin"' >> /home/${USER_NAME}/.bash_history
RUN echo 'make -C build-cross-docker -j4' >> /home/${USER_NAME}/.bash_history
RUN echo '# mount volumes: --volume <everest-ui source dir>:/everest-ui --volume <Charge SOM sysroot>:/sysroot:ro --volume <tooling repo>:/tooling:ro' >> /home/${USER_NAME}/.bash_history

# RUN mkdir -p /workdir
# RUN mkdir -p /workdir-source
# RUN chown -R ${USER_NAME}:${USER_NAME} /workdir
# RUN chown -R ${USER_NAME}:${USER_NAME} /workdir-source

# use bash -l -c <command> for a command in a login shell
ENTRYPOINT ["/bin/bash", "-l"]

WORKDIR /home/${USER_NAME}
USER $USER_NAME
