FROM debian:bookworm-slim
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential gawk git gettext unzip python3 python3-distutils \
    python-is-python3 python3-cryptography rsync file wget curl \
    libncurses-dev zlib1g-dev libssl-dev libelf-dev libc6-dev ca-certificates \
    xz-utils bzip2 patch quilt sudo locales \
    && rm -rf /var/lib/apt/lists/* \
    && localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8
ENV LANG=en_US.utf8 FORCE_UNSAFE_CONFIGURE=1
WORKDIR /entware
